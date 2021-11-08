#include <stdio.h>
#include <string.h>
#include "od.h"
#include "fmt.h"
#include "msg.h"
#include "thread.h"

#ifdef BOARD_LORA3A_SENSOR1
#include "mutex.h"
#include "periph/adc.h"
#include "periph/cpuid.h"
#include "periph/gpio.h"
#include "periph/pm.h"
#include "periph/rtt.h"
#include "periph/rtc_mem.h"

#include "hdc.h"
#ifdef DEBUG_SAML21
#include "debug_saml21.h"
#endif
#endif


#include "common.h"
#include "protocol.h"

static lora_state_t lora;

#ifdef BOARD_LORA3A_SENSOR1
#ifndef EMB_ADDRESS
#define EMB_ADDRESS 1
#endif
#ifndef SLEEP_TIME_SEC
#define SLEEP_TIME_SEC 20
#endif
#ifndef LISTEN_TIME_MSEC
#define LISTEN_TIME_MSEC 110
#endif
static struct {
    uint16_t sleep_seconds;
    uint16_t message_counter;
    uint8_t last_rssi;
    int8_t last_snr;
    uint8_t tx_power;
} persist;

static struct {
    uint8_t cpuid[CPUID_LEN];
    int32_t vcc;
    int32_t vpanel;
    double temp;
    double hum;
} measures;
#endif

#ifdef BOARD_LORA3A_DONGLE
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

#ifdef BOARD_LORA3A_DONGLE
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
#ifdef BOARD_LORA3A_SENSOR1
void read_measures(void)
{
    // get cpuid
    cpuid_get(&measures.cpuid);
    // read vcc
    measures.vcc = adc_sample(0, ADC_RES_12BIT);
    // read vpanel
    gpio_init(GPIO_PIN(PA, 19), GPIO_OUT);
    gpio_set(GPIO_PIN(PA, 19));
    ztimer_sleep(ZTIMER_MSEC, 10);
    measures.vpanel = adc_sample(1, ADC_RES_12BIT);
    gpio_clear(GPIO_PIN(PA, 19));
    // read temp, hum
    measures.temp=0;
    measures.hum=0;
    read_hdc(&measures.temp, &measures.hum);
}

void parse_command(const char *ptr, size_t len) {
    if((len > 2) && (strlen(ptr) == (size_t)(len-1)) && (ptr[0] == '@') && (ptr[len-2] == '$')) {
        uint32_t seconds = strtoul(ptr+1, NULL, 0);
        printf("Instructed to sleep for %lu seconds\n", seconds);
        persist.sleep_seconds = (seconds > 0 ) && (seconds < 36000) ? (uint16_t)seconds : SLEEP_TIME_SEC;
        if ((uint32_t)persist.sleep_seconds != seconds) {
            printf("Corrected sleep value: %u seconds\n", persist.sleep_seconds);
        }
    }
}

void backup_mode(uint32_t seconds)
{
    uint8_t extwake = 6;
    gpio_init(GPIO_PIN(PA, extwake), GPIO_IN_PU);
    // PA06 aka BTN0 can wake up the board
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
    size_t num = sizeof(PORT->Group)/sizeof(PortGroup);
    size_t num1 = sizeof(PORT->Group[0].PINCFG)/sizeof(PORT_PINCFG_Type);
    for (size_t i=0; i<num; i++) {
        for (size_t j=0; j<num1; j++) {
            if (i != 0 || j != extwake) {
                PORT->Group[i].PINCFG[j].reg = 0;
            }
        }
    }
    // add pullups to console pins
    for (size_t i=0; i<UART_NUMOF; i++) {
        gpio_init(uart_config[i].rx_pin, GPIO_IN_PU);
        gpio_init(uart_config[i].tx_pin, GPIO_IN_PU);
    }
    pm_set(0);
}
#endif

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

#ifdef BOARD_LORA3A_SENSOR1
    lora_off();
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
        "cpuid:%s,vcc:%ld,vpanel:%ld,temp:%.2f,hum:%.2f,txpower:%d,sleep:%lu",
        cpuid, measures.vcc, measures.vpanel, measures.temp,
        measures.hum, persist.tx_power, seconds
    );
    lora.power = persist.tx_power;
    if (lora_init(&(lora)) == 0) {
        send_to(EMB_BROADCAST, message, strlen(message)+1);
        // wait for a command
        lora_listen();
        if (ztimer_msg_receive_timeout(ZTIMER_MSEC, &msg, LISTEN_TIME_MSEC) != -ETIME) {
            // parse command
            packet = (embit_packet_t *)msg.content.ptr;
            persist.last_rssi = packet->rssi;
            persist.last_snr = packet->snr;
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
    persist.tx_power = lora_get_power();
    rtc_mem_write(0, (char *)&persist, sizeof(persist));
    // enter deep sleep
    backup_mode(seconds);
#else
#ifdef BOARD_LORA3A_DONGLE
    memset(num_messages, 0, sizeof(num_messages));
    memset(last_message_no, 0, sizeof(last_message_no));
    memset(lost_messages, 0, sizeof(lost_messages));
    if (lora_init(&(lora)) != 0) {
        puts("ERROR: cannot initialize radio.");
        puts("STOP.");
        return 1;
    }
    lora_listen();
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
            char command[] = "@30$"; // sleep for 30 seconds
            send_to(h->src, command, strlen(command)+1);
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
#endif
    return 0;
}
