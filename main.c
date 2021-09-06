#include <stdio.h>
#include <string.h>
#include "od.h"
#include "fmt.h"

#ifdef BOARD_LORA3A_SENSOR1
#include "mutex.h"
#include "periph/adc.h"
#include "periph/cpuid.h"
#include "periph/gpio.h"
#include "periph/pm.h"
#include "periph/rtt.h"
#include "periph/rtc_mem.h"

#include "hdc2021.h"
#ifdef DEBUG_SAML21
#include "debug_saml21.h"
#endif
#endif

#ifdef BOARD_LORA3A_DONGLE
#include "thread.h"
#endif

#include "common.h"
#include "protocol.h"

consume_data_cb_t *packet_consumer;

static lora_state_t lora;

#ifdef BOARD_LORA3A_SENSOR1
static mutex_t sleep_lock = MUTEX_INIT;
#ifndef EMB_ADDRESS
#define EMB_ADDRESS 1
#endif
static struct {
    uint32_t sleep_seconds;
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
    uint8_t pow;
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

#ifndef LISTEN_TIME_MSEC
#define LISTEN_TIME_MSEC 60
#endif

static uint16_t emb_counter = 0;

void send_to(uint8_t dst, char *buffer, size_t len)
{
    embit_header_t h;
    h.signature = EMB_SIGNATURE;
    h.counter = ++emb_counter;
    h.network = EMB_NETWORK;
    h.dst = dst;
    h.src = EMB_ADDRESS;
    iolist_t packet, payload;
    packet.iol_base = &h;
    packet.iol_len = sizeof(h);
    packet.iol_next = &payload;
    payload.iol_base = buffer;
    payload.iol_len = len;
    payload.iol_next = NULL;
    printf("Sending packet #%u to 0x%02X:\n", emb_counter, dst);
    printf("%s\n", buffer);
    to_lora((const iolist_t *)&packet);
    puts("Sent.");
}

ssize_t packet_received(const void *buffer, size_t len, uint8_t *rssi, int8_t *snr)
{
    embit_packet_t *p = (embit_packet_t *)buffer;
    embit_header_t h = p->header;
    ssize_t n = len - EMB_HEADER_LEN;

    // discard invalid packets
    if ((h.signature != EMB_SIGNATURE) || (n <= 0) || (n >= MAX_PAYLOAD_LEN)) { return 0; }

    // discard packets if not for us and not broadcast
    if ((h.dst != EMB_ADDRESS) && (h.dst != EMB_BROADCAST)) { return 0; }

    // dump message to stdout
#ifdef BOARD_LORA3A_DONGLE
    num_messages[h.src]++;
    if ((uint32_t)h.counter > (uint32_t)(last_message_no[h.src] + 1)) {
        lost_messages[h.src] += h.counter - last_message_no[h.src] - 1;
    }
    last_message_no[h.src] = h.counter;
#endif
    char *ptr = p->payload;
    printf("{\"CNT\":%u,\"NET\":%u,\"DST\":%u,\"SRC\":%u,\"RSSI\":%d,\"SNR\":%d,\"DATA\"=\"%s\"}\n", h.counter, h.network, h.dst, h.src, *rssi, *snr, ptr);
#ifdef BOARD_LORA3A_SENSOR1
    // hold the mutex: don't enter sleep yet, we need to do some work
    mutex_lock(&sleep_lock);
    // parse command
    if((n > 2) && (strlen(ptr) == (size_t)(n-1)) && (ptr[0] == '@') && (ptr[n-2] == '$')) {
        uint32_t seconds = strtoul(ptr+1, NULL, 0);
        printf("Instructed to sleep for %lu seconds\n", seconds);
        persist.sleep_seconds = seconds;
    } else {
        persist.sleep_seconds = 0;
    }
    persist.last_rssi = *rssi;
    persist.last_snr = *snr;
    // release the mutex
    mutex_unlock(&sleep_lock);
#else
#ifdef BOARD_LORA3A_DONGLE
    // send command
    char command[] = "@10$";
    send_to(h.src, command, strlen(command)+1);
#endif
#endif
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
    read_hdc2021(&measures.temp, &measures.hum);
    // read tx power
    measures.pow = lora_get_power();
}

void send_measures(void)
{
    char cpuid[CPUID_LEN*2+1];
    char message[MAX_PAYLOAD_LEN];
    fmt_bytes_hex(cpuid, measures.cpuid, CPUID_LEN);
    cpuid[CPUID_LEN*2]='\0';
    snprintf(
        message, MAX_PAYLOAD_LEN,
        "cpuid:%s,vcc:%ld,vpanel:%ld,temp:%.2f,hum:%.2f,pow:%d",
        cpuid, measures.vcc, measures.vpanel, measures.temp,
        measures.hum, measures.pow
    );
    send_to(EMB_BROADCAST, message, strlen(message)+1);
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
    lora.data_cb          = *from_lora;

    packet_consumer = *packet_received;
    if (lora_init(&(lora)) != 0) { return 1; }


#ifdef BOARD_LORA3A_SENSOR1
    if (RSTC->RCAUSE.reg == RSTC_RCAUSE_BACKUP) {
        // read persistent values
        rtc_mem_read(0, (char *)&persist, sizeof(persist));
        bool empty = 1;
        for (uint8_t i=0; (empty == 1) && (i < sizeof(persist)); i++) {
            empty = ((char *)&persist)[i] == 0;
        }
        if (!empty) {
            emb_counter = persist.message_counter;
            lora_set_power(persist.tx_power);
        }
    } else {
        memset(&persist, 0, sizeof(persist));
    }
    read_measures();
    send_measures();
    lora_listen();
    ztimer_sleep(ZTIMER_MSEC, LISTEN_TIME_MSEC);
    // hold the mutex: don't parse packets because we're going to sleep
    mutex_lock(&sleep_lock);
    // adjust sleep interval according to available energy
    uint32_t seconds = persist.sleep_seconds;
    if (seconds) { printf("Sleep value from persistence: %lu seconds\n", seconds); }
    seconds = (seconds > 0) && (seconds < 1000) ? seconds : 5;
    uint8_t lowpowerVcc = (measures.vcc < 2900) ? 1 : 0;
    uint8_t lowpowerVpanel = (measures.vpanel < 2000) ? 1 : 0;
    seconds = lowpowerVcc ? seconds*4 : seconds;
    seconds = lowpowerVpanel ? seconds*5 : seconds;
    if (seconds) {
        printf(
            "Sleep value from RTC: %lu seconds; lpVcc = %d; lpVpanel = %d\n",
            seconds, lowpowerVcc, lowpowerVpanel
        );
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
    lora_listen();
    for (;;) {
        print_stats();
        ztimer_sleep(ZTIMER_MSEC, 60000);
    }
#endif
#endif
    return 0;
}
