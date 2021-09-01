#include <stdio.h>
#include <string.h>
#include "od.h"

#ifdef BOARD_LORA3A_SENSOR1
#include "periph/pm.h"
#include "periph/gpio.h"
#include "periph/adc.h"
#include "periph/rtt.h"
#include "periph/rtc_mem.h"

#include "hdc2021.h"
#endif

#include "common.h"
#include "protocol.h"

consume_data_cb_t *packet_consumer;

static lora_state_t lora;

ssize_t packet_received(const void *buffer, size_t len)
{
    (void)buffer;
    (void)len;

    // dump message to stdout
    puts("Received packet:");
    od_hex_dump(buffer, len < 128 ? len : 128, 0);
#ifdef BOARD_LORA3A_SENSOR1
    // parse command
    char *ptr = (char *)buffer;
    if((ptr[0] == '@') && (ptr[strlen(ptr)-1] == '$')) {
        uint32_t seconds = strtoul(ptr+1, NULL, 0);
        printf("Instructed to sleep for %lu seconds\n", seconds);
        rtc_mem_write(0, (char *)&seconds, sizeof(seconds));
    }
#else
#ifdef BOARD_LORA3A_DONGLE
    // send command
    char command[] = "@1$";
    puts("Sending packet:");
    printf("%s\n", command);
    to_lora(command, strlen(command));
#endif
#endif
    return 0;
}

#ifdef BOARD_LORA3A_SENSOR1
void send_measures(void)
{
    char buffer[MAX_PACKET_LEN];
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
    sprintf(buffer, "prova vcc=%ld, vpanel=%ld, temp=%.2f, hum=%.2f", vcc, vpanel, temp, hum);
    puts("Sending packet:");
    printf("%s\n", buffer);
    to_lora(buffer, strlen(buffer));
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
    ztimer_sleep(ZTIMER_MSEC, 1000);
    uint32_t seconds;
    rtc_mem_read(0, (char *)&seconds, sizeof(seconds));
    printf("Saved sleep value: %lu seconds\n", seconds);
    seconds = (seconds > 0) && (seconds < 120) ? seconds : 5;
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
