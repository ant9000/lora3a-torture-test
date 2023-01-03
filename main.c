#include <stdio.h>
#include <string.h>
#include "od.h"
#include "fmt.h"
#include "msg.h"
#include "thread.h"

#if defined(BOARD_LORA3A_SENSOR1) || defined(BOARD_LORA3A_H10)
#include "mutex.h"
#include "periph/adc.h"
#include "periph/cpuid.h"
#include "periph/gpio.h"
#include "periph/pm.h"
#include "periph/rtt.h"
#include "periph/rtc_mem.h"

#include "hdc.h"
#include "shell.h"

#ifdef DEBUG_SAML21
#include "saml21_cpu_debug.h"
#endif
#endif
#include "saml21_backup_mode.h"


#include "common.h"
#include "protocol.h"

#include "saul_reg.h"

#include "board.h"

static lora_state_t lora;

/* use { .pin=EXTWAKE_NONE } to disable */
#define EXTWAKE { \
    .pin=EXTWAKE_PIN6, \
    .polarity=EXTWAKE_HIGH, \
    .flags=EXTWAKE_IN_PU }

static saml21_extwake_t extwake = EXTWAKE;

#if defined(BOARD_LORA3A_SENSOR1) || defined(BOARD_LORA3A_H10)
#ifndef EMB_ADDRESS
#define EMB_ADDRESS 1
#endif
#ifndef SLEEP_TIME_SEC
#define SLEEP_TIME_SEC 5
#endif
#ifndef LISTEN_TIME_MSEC
#ifndef CUSTOMER
#define LISTEN_TIME_MSEC 150
#else
// use 450 if BW 125kHz
//#define LISTEN_TIME_MSEC 450
#define LISTEN_TIME_MSEC 150
#endif
#endif
static struct {
    uint16_t sleep_seconds;
    uint16_t message_counter;
    int16_t last_rssi;
    int8_t last_snr;
    uint8_t tx_power;
    uint8_t boost;
    uint8_t retries;
} persist;

static struct {
    uint8_t cpuid[CPUID_LEN];
    int32_t vcc;
    int32_t vpanel;
    double temp;
    double hum;
} measures;
#endif


#if defined(BOARD_LORA3A_DONGLE) || defined(H10RX)
#ifndef EMB_ADDRESS
#define EMB_ADDRESS 254
#endif
static uint16_t num_messages[255];
static uint16_t last_message_no[255];
static uint16_t lost_messages[255];
#endif

#ifndef EMB_NETWORK
#define EMB_NETWORK 1
#endif

static uint16_t emb_counter = 0;
static embit_packet_t q_packet;
static int gateway_received_rssi;
static char q_payload[MAX_PACKET_LEN];
static kernel_pid_t main_pid;

void send_to(uint8_t dst, char *buffer, size_t len)
{
    embit_header_t header;
    header.signature = EMB_SIGNATURE;
    header.counter = ++emb_counter;
    header.network = EMB_NETWORK;
    header.dst = dst;
    header.src = EMB_ADDRESS;
    printf("Sending %d+%d bytes packet #%u to 0x%02X:\n", EMB_HEADER_LEN, len, emb_counter, dst);
    printf("%s\n", buffer);
    protocol_out(&header, buffer, len);
    puts("Sent.");
}

ssize_t packet_received(const embit_packet_t *packet)
{
    // discard packets if not for us and not broadcast
    if ((packet->header.dst != EMB_ADDRESS) && (packet->header.dst != EMB_BROADCAST)) { return 0; }


    // dump message to stdout
    printf(
        "{\"CNT\":%u,\"NET\":%u,\"DST\":%u,\"SRC\":%u,\"RSSI\":%d,\"SNR\":%d,\"LEN\"=%d,\"DATA\"=\"%s\"}\n",
        packet->header.counter, packet->header.network, packet->header.dst, packet->header.src,
        packet->rssi, packet->snr, packet->payload_len, packet->payload
    );
    msg_t msg;
    memcpy(q_payload, packet->payload, packet->payload_len);
    memcpy(&q_packet, packet, sizeof(embit_packet_t));
    q_packet.payload = q_payload;
    gateway_received_rssi = packet->rssi;
    msg.content.ptr = &q_packet;
    msg_send(&msg, main_pid);
    return 0;
}


#if defined(BOARD_LORA3A_DONGLE) || defined(H10RX)
void print_stats(void)
{
    uint8_t nodes=0;
    uint32_t received=0, lost=0;
    puts("# --------------------------------------");
    for (int src=0; src<255; src++) {
        if (num_messages[src]) {
            nodes++;
            received += num_messages[src];
            lost += lost_messages[src];
            printf(
                "# SRC 0x%02X: messages received: %u, last message: %u, lost messages: %u\n",
                src, num_messages[src], last_message_no[src], lost_messages[src]
            );
        }
    }
    printf(
        "# TOTAL: nodes: %d, received messages: %lu, lost messages: %lu\n",
        nodes, received, lost
    );
    puts("# --------------------------------------");
}
#endif
#if defined(BOARD_LORA3A_SENSOR1)
#define VPANEL_ENABLE  GPIO_PIN(PA, 19)
#endif
#if defined(BOARD_LORA3A_H10)
#define VPANEL_ENABLE  GPIO_PIN(PA, 27)
#endif


#if defined(BOARD_LORA3A_SENSOR1) || defined(BOARD_LORA3A_H10)
#define ADC_VCC    (0)
#define ADC_VPANEL (1)

void read_measures(void)
{
    // get cpuid
    cpuid_get(&measures.cpuid);
    // read vcc
    measures.vcc = adc_sample(ADC_VCC, ADC_RES_12BIT)*4000/4095;  // corrected value (1V = 4095 counts)
    // read vpanel
    gpio_init(VPANEL_ENABLE, GPIO_OUT);
    gpio_set(VPANEL_ENABLE);
    ztimer_sleep(ZTIMER_MSEC, 30);
    measures.vpanel = adc_sample(ADC_VPANEL, ADC_RES_12BIT)*3933/4095; // adapted to real resistor partition value (75k over 220k)
    gpio_clear(VPANEL_ENABLE);

    // read temp, hum
    read_hdc(&measures.temp, &measures.hum);
}

void parse_command(char *ptr, size_t len) {
	char *token;
	int8_t txpow=0;
    if((len > 2) && (strlen(ptr) == (size_t)(len-1)) && (ptr[0] == '@') && (ptr[len-2] == '$')) {
		token = strtok(ptr+1, ",");
        uint32_t seconds = strtoul(token, NULL, 0);
//seconds = 5;        
        printf("Instructed to sleep for %lu seconds\n", seconds);

        persist.sleep_seconds = (seconds > 0 ) && (seconds < 36000) ? (uint16_t)seconds : SLEEP_TIME_SEC;
        if ((uint32_t)persist.sleep_seconds != seconds) {
            printf("Corrected sleep value: %u seconds\n", persist.sleep_seconds);
        }
        token = strtok(NULL, ",");
        
//token[0] = 'B';
        
        if (token[0]=='B') {
			printf("Boost out selected!\n");
			lora.boost = 1;
		} else {
			printf("RFO out selected!\n");
			lora.boost = 0;
		}
        token = strtok(NULL, "$");
        txpow = atoi(token);
//txpow = 14;
        if (txpow!=0) {
			lora.power = txpow;
			printf("Instructed to tx at level %d\n",txpow);
		}
        // to be added a complete error recovery/received commands validation for transmission errors
    }
}

void backup_mode(uint32_t seconds)
{
	puts("backup_mode entered\n");
    lora_off();
#ifdef DEBUG_SAML21
    saml21_cpu_debug();
#endif
    saml21_backup_mode_enter(extwake, (int)seconds);
}
#endif


int saul_cmd(int num)
{
	saul_reg_t *dev;
	int dim;
	phydat_t res;
	int retval=9999;
    dev = saul_reg_find_nth(num);
    dim = saul_reg_read(dev, &res);
    if (dim <= 0) {
        // errore
        printf("Error on saul_reg_read\n");
    } else {
        printf("Reading from #%i (%s|%s)\n", num, (dev->name ? dev->name : "(no name)"), saul_class_to_str(dev->driver->type));
        phydat_dump(&res, dim);
        retval = res.val[0];
    }
    return retval;
}


int main(void)
{
    memset(&lora, 0, sizeof(lora));
    lora.bandwidth        = DEFAULT_LORA_BANDWIDTH;
    lora.spreading_factor = DEFAULT_LORA_SPREADING_FACTOR;
    lora.coderate         = DEFAULT_LORA_CODERATE;
    lora.channel          = DEFAULT_LORA_CHANNEL;
    lora.power            = DEFAULT_LORA_POWER;
    lora.data_cb          = *protocol_in;

    main_pid = thread_getpid();
    protocol_init(*packet_received);

    msg_t msg;
    embit_packet_t *packet;

	puts("\n");
	printf("LORA3A-TORTURE-TEST Compiled: %s,%s\n", __DATE__, __TIME__);

#if defined(BOARD_LORA3A_SENSOR1) || defined(BOARD_LORA3A_H10) && !defined(H10RX)

	int16_t bmetemp;
	int16_t bmepress;
	int16_t bmehum;
	int16_t bmevoc;

	printf("Sensor set. Address: %d Bandwidth: %d Frequency: %ld Listen Time ms: %d\n", EMB_ADDRESS, DEFAULT_LORA_BANDWIDTH, DEFAULT_LORA_CHANNEL, LISTEN_TIME_MSEC);
    lora_off();

	bmetemp = saul_cmd (4);
	bmepress = saul_cmd (5);
	bmehum = saul_cmd (6);
	bmevoc = saul_cmd (7);

    read_measures();
    if (RSTC->RCAUSE.reg == RSTC_RCAUSE_BACKUP) {
        // read persistent values
        rtc_mem_read(0, (char *)&persist, sizeof(persist));
        bool empty = 1;
        for (uint8_t i=0; (empty == 1) && (i < sizeof(persist)); i++) {
            empty = ((char *)&persist)[i] == 0;
        }
        if (!empty) {
            emb_counter = persist.message_counter;
        }
        lora.boost = persist.boost;  // ROB to recover boost from persist
        printf("Boost value from persistence: %d\n", lora.boost);
    } else {
        memset(&persist, 0, sizeof(persist));
        persist.sleep_seconds = SLEEP_TIME_SEC;
        persist.tx_power = lora.power;
    }
    // adjust sleep interval according to available energy
    uint32_t seconds = persist.sleep_seconds;
    if (seconds) { printf("Sleep value from persistence: %lu seconds\n", seconds); }
    seconds = (seconds > 0) && (seconds < 36000) ? seconds : SLEEP_TIME_SEC;
// first strategy of power saving on vcc value
#if 0
    uint8_t lpVcc = (measures.vcc < 2900) ? 1 : 0;
    if (lpVcc) {
        seconds = seconds * 4 < 0xffff ? seconds * 4 : 0xffff;
    }
#endif
// second strategy of power saving on vcc value
#if 1
	int vccReductionFactor = 1;
	if (measures.vcc < 2800) vccReductionFactor = 120;
	else if (measures.vcc < 2900) vccReductionFactor = 60;
	else if (measures.vcc < 3000) vccReductionFactor = 30;
	else if (measures.vcc < 3100) vccReductionFactor = 15;
	else if (measures.vcc < 3200) vccReductionFactor = 5;
	else vccReductionFactor = 1;
	seconds = seconds * vccReductionFactor;
#endif
    uint8_t lpVpanel = (measures.vpanel < 2000) ? 1 : 0;
    if (lpVpanel) {
        seconds = seconds * 5 < 0xffff ? seconds * 5 : 0xffff;
    }

    printf("vccReductionFactor = %d, lpVpanel = %d, Adjusted sleep value: %lu seconds, persist.sleep_seconds = %d\n", vccReductionFactor, lpVpanel, seconds, persist.sleep_seconds );
    
    // send measures
    char cpuid[CPUID_LEN*2+1];
    char message[MAX_PAYLOAD_LEN];
    fmt_bytes_hex(cpuid, measures.cpuid, CPUID_LEN);
    cpuid[CPUID_LEN*2]='\0';

    snprintf(
        message, MAX_PAYLOAD_LEN,
        "vcc:%ld,vpan:%ld,temp:%.2f,hum:%.2f,txp:%c:%d,rxdb:%d,rxsnr:%d,sleep:%lu,%d,%d,%d,%d",
//        cpuid,
        measures.vcc, measures.vpanel, measures.temp,
        measures.hum, persist.boost?'B':'R', persist.tx_power, persist.last_rssi, persist.last_snr, seconds, bmetemp, bmepress, bmehum, bmevoc
    );
    lora.power = persist.tx_power;
    if (lora_init(&lora) == 0) {
        send_to(EMB_BROADCAST, message, strlen(message)+1);
        // wait for a command
        lora_listen();
        if (ztimer_msg_receive_timeout(ZTIMER_MSEC, &msg, LISTEN_TIME_MSEC) != -ETIME) {
            // parse command
            packet = (embit_packet_t *)msg.content.ptr;
            persist.last_rssi = packet->rssi;
            persist.last_snr = packet->snr;
            persist.retries = 2;
            printf("rx rssi %d, rx snr %d, retries=%d\n",packet->rssi, packet->snr, persist.retries);
            char *ptr = packet->payload;
            size_t len = packet->payload_len;
            parse_command(ptr, len);
        } else {
            puts("No command received.");
            if (persist.retries > 0) {
				persist.retries--;
			} else {
				// elapsed max number of retries	
				// set power to maximum and after 10 seconds send again.
				lora.boost = 1;
				lora.power = 14;
			}
            seconds = 25 + EMB_ADDRESS % 10;
			printf("retries = %d, new seconds for retry = %ld\n", persist.retries, seconds);
        }
    } else {
        puts("ERROR: cannot initialize radio.");
    }
    // save persistent values
    persist.message_counter = emb_counter;
//    persist.tx_power = lora_get_power();
    persist.tx_power = lora.power;  // lora_get_power is not used for transmission
    persist.boost = lora.boost;
     rtc_mem_write(0, (char *)&persist, sizeof(persist));
    // enter deep sleep

    backup_mode(seconds);
#endif

#if defined(BOARD_LORA3A_DONGLE) || defined(BOARD_LORA3A_H10) && defined(H10RX)
printf("Gateway set. Address: %d Bandwidth: %d Frequency: %ld \n", EMB_ADDRESS, DEFAULT_LORA_BANDWIDTH, DEFAULT_LORA_CHANNEL);
    char str_to_node[20];
    int interval_time = 60;

    memset(num_messages, 0, sizeof(num_messages));
    memset(last_message_no, 0, sizeof(last_message_no));
    memset(lost_messages, 0, sizeof(lost_messages));
    if (lora_init(&(lora)) != 0) {
        puts("ERROR: cannot initialize radio.");
        puts("STOP.");
        return 1;
    }
    lora_listen();
    lora.boost=1;
    ztimer_now_t last_stats_run = 0;
    for (;;) {
        if (ztimer_msg_receive_timeout(ZTIMER_MSEC, &msg, 1000) != -ETIME) {
            packet = (embit_packet_t *)msg.content.ptr;
            embit_header_t *h = &packet->header;
            num_messages[h->src]++;
            if ((uint32_t)h->counter > (uint32_t)(last_message_no[h->src] + 1)) {
                lost_messages[h->src] += h->counter - last_message_no[h->src] - 1;
            }
            last_message_no[h->src] = h->counter;
            
		// automatic sensor-node power adaptation for distance (fixed target now at -90dBm)
		//    printf("payload=%s\n", q_payload);
			char mypayload[30][7];
			char *token = strtok(q_payload, ":,");
			int i=0;
			int node_boostmode=0;
			int node_power=14;
			int node_rxdb=-90;
			while( token != NULL) {
				sprintf(mypayload[i], "%s", token);
				i++;
				token = strtok(NULL, ":,");
			}	 
		//	printf("%s,%s,%s\n",mypayload[9],mypayload[10],mypayload[12]);
			if (mypayload[9][0] == 'B') node_boostmode = 1; else node_boostmode = 0;
			node_power = atoi(mypayload[10]);
			if (node_power == 0) node_power = 14; // if atoi returns zero for no conversion made
			node_rxdb = atoi(mypayload[12]);
			if (node_rxdb == 0) {  // if last time the node_sensor has not received the ack set its power to the maximum
				node_power = 14;
				node_boostmode = 1;
			}
		//	printf("extracted: rx_rssi= %d, boost=%d, power=%d, rxdb=%d\n", gateway_received_rssi, node_boostmode, node_power, node_rxdb);
			// strategy to reduce power if node is near (> -90dBm)
			if (gateway_received_rssi > -90) {
				// we have to reduce the node tx power
				if (node_power > 1) node_power--;
				else if (node_power == 1) {
					if (node_boostmode == 1) { node_boostmode = 0; node_power = 14; }
		//			else printf ("Already at R,1 power! \n");
				}	
			} else {
				// we have to increase the node tx power
				if (node_power < 14) node_power++;
				else if (node_power == 14) {
					if (node_boostmode == 0) { node_boostmode = 1; node_power = 1; }
		//			else printf ("Already at B,14 power! \n");
				}	
			}
		//	printf("New power to set: %c, %d\n", (node_boostmode ? 'B' : 'R'),	node_power);
			switch (h->src) {
				case 41: interval_time = 300;	break;
				case 42: interval_time = 300;	break;
				case 43: interval_time = 720;	break;
				case 44: interval_time = 300;	break;
				case 45: interval_time = 300;	break;
				case 46: interval_time = 300;	break;
				case 47: interval_time = 300;	break;
//				case 71: interval_time = 5;		break;
				case 100: interval_time = 720;	break;
				default: interval_time = 60;	break;
			}
			sprintf(str_to_node,"@%d,%c,%d$", interval_time, (node_boostmode ? 'B' : 'R'), node_power);
			printf("new node settings: %s\n", str_to_node); 
			send_to(h->src, str_to_node, strlen(str_to_node)+1);
            lora_listen();
        } else {
            ztimer_now_t now = ztimer_now(ZTIMER_MSEC);
            if (now >= last_stats_run + 60000) {
                last_stats_run = now;
                print_stats();
            }
        }
    }
#endif
    return 0;
}
