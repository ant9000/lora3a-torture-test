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

#include "common.h"
#include "protocol.h"

consume_data_cb_t *packet_consumer;

static lora_state_t lora;

#ifdef BOARD_LORA3A_SENSOR1
static mutex_t sleep_lock = MUTEX_INIT;
#ifndef EMB_ADDRESS
#define EMB_ADDRESS 1
#endif
#endif

#ifdef BOARD_LORA3A_DONGLE
#ifndef EMB_ADDRESS
#define EMB_ADDRESS 254
#endif
static uint32_t num_messages = 0;
#endif

#ifndef EMB_NETWORK
#define EMB_NETWORK 1
#endif

static uint16_t emb_counter = 0;

void send_to(uint8_t dst, char *buffer, size_t len)
{
    char packet_buffer[MAX_PACKET_LEN];
    embit_packet_t *p = (embit_packet_t *)packet_buffer;
    p->header.signature = EMB_SIGNATURE;
    p->header.counter = emb_counter++;
    p->header.network = EMB_NETWORK;
    p->header.dst = dst;
    p->header.src = EMB_ADDRESS;
    memcpy(p->payload, buffer, len);
    to_lora(packet_buffer, EMB_HEADER_LEN+len);
}

ssize_t packet_received(const void *buffer, size_t len)
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
    printf("Num messages received: %ld\n", ++num_messages);
#endif
    puts("Received packet:");
    printf("CNT:%u NET:%u DST:%u SRC:%u\n", h.counter, h.network, h.dst, h.src);
    char *ptr = p->payload;
    od_hex_dump(ptr, n < 128 ? n : 128, 0);
#ifdef BOARD_LORA3A_SENSOR1
    // hold the mutex: don't enter sleep yet, we need to do some work
    mutex_lock(&sleep_lock);
    // parse command
    if((ptr[0] == '@') && (ptr[n-1] == '$')) {
        uint32_t seconds = strtoul(ptr+1, NULL, 0);
        printf("Instructed to sleep for %lu seconds\n", seconds);
        rtc_mem_write(0, (char *)&seconds, sizeof(seconds));
    }
    // release the mutex
    mutex_unlock(&sleep_lock);
#else
#ifdef BOARD_LORA3A_DONGLE
    // send command
    char command[] = "@1$";
    puts("Sending packet:");
    printf("%s\n", command);
    send_to(h.src, command, strlen(command));
#endif
#endif
    return 0;
}

#ifdef BOARD_LORA3A_SENSOR1
void send_measures(void)
{
    // get cpuid
    uint8_t cpuid[CPUID_LEN];
    char cpuid_str[CPUID_LEN*2+1];
    cpuid_get(&cpuid);
    fmt_bytes_hex(cpuid_str, cpuid, CPUID_LEN);
    cpuid_str[CPUID_LEN*2]='\0';
    // read vcc
    int32_t vcc = adc_sample(0, ADC_RES_12BIT);
    // read vpanel
    gpio_init(GPIO_PIN(PA, 19), GPIO_OUT);
    gpio_set(GPIO_PIN(PA, 19));
    ztimer_sleep(ZTIMER_MSEC, 10);
    int32_t vpanel = adc_sample(1, ADC_RES_12BIT);
    gpio_clear(GPIO_PIN(PA, 19));
    // read temp, hum
    double temp=0, hum=0;
    read_hdc2021(&temp, &hum);
    // send packet
    char message[MAX_PAYLOAD_LEN];
    snprintf(message, MAX_PAYLOAD_LEN, "cpuid=%s vcc=%ld vpanel=%ld temp=%.2f hum=%.2f", cpuid_str, vcc, vpanel, temp, hum);
    puts("Sending packet:");
    printf("%s\n", message);
    send_to(EMB_BROADCAST, message, strlen(message));
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
    send_measures();
    lora_listen();
    ztimer_sleep(ZTIMER_MSEC, 100);
    // hold the mutex: don't parse packets because we're going to sleep
    mutex_lock(&sleep_lock);
    // read the sleep interval duration from rtc_mem
    uint32_t seconds;
    rtc_mem_read(0, (char *)&seconds, sizeof(seconds));
    if (seconds) { printf("Sleep value from RTC: %lu seconds\n", seconds); }
    seconds = (seconds > 0) && (seconds < 120) ? seconds : 5;
    // enter deep sleep
    backup_mode(seconds);
#else
#ifdef BOARD_LORA3A_DONGLE
    lora_listen();
    for (;;) {
        // do nothing
    }
#endif
#endif
    return 0;
}
