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
#include "debug_saml21.h"
#endif
#endif


#include "common.h"
#include "protocol.h"

#include "saul_reg.h"

#include "board.h"

static lora_state_t lora;

#if defined(BOARD_LORA3A_SENSOR1) || defined(BOARD_LORA3A_H10)
#ifndef EMB_ADDRESS
#define EMB_ADDRESS 1
#endif
#ifndef SLEEP_TIME_SEC
#define SLEEP_TIME_SEC 5
#endif
#ifndef LISTEN_TIME_MSEC
#define LISTEN_TIME_MSEC 110
#endif
static struct {
    uint16_t sleep_seconds;
    uint16_t message_counter;
    int16_t last_rssi;
    int8_t last_snr;
    uint8_t tx_power;
    uint8_t boost;
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
        printf("Instructed to sleep for %lu seconds\n", seconds);

        persist.sleep_seconds = (seconds > 0 ) && (seconds < 36000) ? (uint16_t)seconds : SLEEP_TIME_SEC;
        if ((uint32_t)persist.sleep_seconds != seconds) {
            printf("Corrected sleep value: %u seconds\n", persist.sleep_seconds);
        }
        token = strtok(NULL, ",");
        if (token[0]=='B') {
			printf("Boost out selected!\n");
			lora.boost = 1;
		} else {
			printf("RFO out selected!\n");
			lora.boost = 0;
		}
        token = strtok(NULL, "$");
        txpow = atoi(token);
        if (txpow!=0) {
			lora.power = txpow;
			printf("Instructed to tx at level %d\n",txpow);
		}
        // to be added a complete error recovery/received commands validation for transmission errors
    }
}

void backup_mode(uint32_t seconds)
{
    uint8_t extwake = 6;
    // PA06 aka BTN0 can wake up the board
    gpio_init(GPIO_PIN(PA, extwake), GPIO_IN_PU);
    RSTC->WKEN.reg = 1 << extwake;
    RSTC->WKPOL.reg &= ~(1 << extwake);
    // schedule a wakeup alarm
    rtt_set_counter(0);
    rtt_set_alarm(RTT_SEC_TO_TICKS(seconds), NULL, NULL);

    puts("Now entering backup mode.");
    // turn off radio
    lora_off();
#ifdef DEBUG_SAML21
    debug_saml21();
#endif
    // turn off PORT pins
//    size_t num = sizeof(PORT->Group)/sizeof(PortGroup);
//    size_t num1 = sizeof(PORT->Group[0].PINCFG)/sizeof(PORT_PINCFG_Type);
//    for (size_t i=0; i<num; i++) {
//        for (size_t j=0; j<num1; j++) {
//            if (i != 0 || j != extwake) {
//                PORT->Group[i].PINCFG[j].reg = 0;
//            }
//        }
//    }
    // add pullups to console pins
//    for (size_t i=0; i<UART_NUMOF; i++) {
//        gpio_init(uart_config[i].rx_pin, GPIO_IN_PU);
//        gpio_init(uart_config[i].tx_pin, GPIO_IN_PU);
//    }
//#if defined(BOARD_SAMR34_XPRO) || defined (BOARD_LORA3A_H10)
//    gpio_init(TCXO_PWR_PIN, GPIO_IN_PD);
//    gpio_init(TX_OUTPUT_SEL_PIN, GPIO_IN_PD);
//#endif
// Rob switch off pins, not necessary anymore
//    gpio_init(GPIO_PIN(PA, 9), GPIO_OUT);
//    gpio_clear(GPIO_PIN(PA, 9));
//    gpio_init(GPIO_PIN(PA, 13), GPIO_OUT);
//    gpio_clear(GPIO_PIN(PA, 13));

//    gpio_init(GPIO_PIN(PA, 4), GPIO_IN_PU);
//    gpio_init(GPIO_PIN(PA, 5), GPIO_IN_PU);
//    gpio_init(GPIO_PIN(PA, 6), GPIO_IN_PU);
//    gpio_init(GPIO_PIN(PA, 7), GPIO_IN_PU);
//    gpio_init(GPIO_PIN(PA, 8), GPIO_IN_PD);

//    gpio_init(GPIO_PIN(PB, 2), GPIO_IN_PU);
//    gpio_init(GPIO_PIN(PB, 3), GPIO_IN_PU);
//    gpio_init(GPIO_PIN(PB, 22), GPIO_IN_PU);
//    gpio_init(GPIO_PIN(PB, 23), GPIO_IN_PU);

    pm_set(0);
}
#endif

int saul_cmd(int num)
{
	saul_reg_t *dev;
	int dim;
	phydat_t res;
	int retval=9999;
//    num = 4; // temp
    dev = saul_reg_find_nth(num);
    dim = saul_reg_read(dev, &res);
//    printf("dim = %d\n", dim);
    if (dim <= 0) {
        // errore
        printf("Error on saul_reg_read\n");
    } else {
//        printf("Phydat: %d %d %d\n", res.val[0], res.unit, res.scale);
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


puts("Sensor set.");
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
    uint8_t lpVcc = (measures.vcc < 2900) ? 1 : 0;
    uint8_t lpVpanel = (measures.vpanel < 2000) ? 1 : 0;
    if (lpVcc) {
        seconds = seconds * 4 < 0xffff ? seconds * 4 : 0xffff;
    }
    if (lpVpanel) {
        seconds = seconds * 5 < 0xffff ? seconds * 5 : 0xffff;
    }
    if (persist.sleep_seconds != seconds) {
        printf(
            "Adjusted sleep value: %lu seconds; lpVcc = %d; lpVpanel = %d\n",
            seconds, lpVcc, lpVpanel
        );
    }
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
            printf("rx rssi %d, rx snr %d\n",packet->rssi, packet->snr);
            char *ptr = packet->payload;
            size_t len = packet->payload_len;
            parse_command(ptr, len);
        } else {
            puts("No command received.");
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
//#else

#if defined(BOARD_LORA3A_DONGLE) || defined(BOARD_LORA3A_H10) && defined(H10RX)
puts("Gateway set.");
    char str41[]="@300,B,1$";
    char str42[]="@300,B,1$";
    char str43[]="@720,R,14$";
    char str44[]="@300,B,1$";
    char str45[]="@300,B,1$";
    char str46[]="@300,B,1$";
    char str47[]="@300,B,1$";
    char str49[]="@10,B,1$";
    char str50[]="@60,B,1$";
    char str62[]="@300,B,1$";
    char str100[]="@720,R,14$";
    char strdefault[]="@300,B,1$";
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
            // send command
            switch (h->src) {
					case 41:
						send_to(h->src, str41, strlen(str41)+1);
						break;
					case 42:
						send_to(h->src, str42, strlen(str42)+1);
						break;
					case 43:
						send_to(h->src, str43, strlen(str43)+1);
						break;
					case 44:
						send_to(h->src, str44, strlen(str44)+1);
						break;
					case 45:
						send_to(h->src, str45, strlen(str45)+1);
						break;
					case 46:
						send_to(h->src, str46, strlen(str46)+1);
						break;
					case 47:
						send_to(h->src, str47, strlen(str47)+1);
						break;
					case 49:
						send_to(h->src, str49, strlen(str49)+1);
						break;
					case 50:
						send_to(h->src, str50, strlen(str50)+1);
						break;
					case 62:
						send_to(h->src, str62, strlen(str62)+1);
						break;
					case 100:
						send_to(h->src, str100, strlen(str100)+1);
						break;
					default:
						send_to(h->src, strdefault, strlen(strdefault)+1);
						break;
			}

            lora_listen();
        } else {
            ztimer_now_t now = ztimer_now(ZTIMER_MSEC);
            if (now >= last_stats_run + 60000) {
                last_stats_run = now;
                print_stats();
            }
        }
    }
//#endif
//#endif
#endif
    return 0;
}
