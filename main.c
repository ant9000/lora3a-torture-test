#include <stdio.h>
#include <string.h>
#include "od.h"

#ifdef BOARD_LORA3A_SENSOR1
#include "periph/pm.h"
#include "periph/gpio.h"
#include "periph/adc.h"
#include "periph/rtc_mem.h"
#include "hdc2021.h"
#endif

#include "common.h"
#include "protocol.h"

static lora_state_t lora;
consume_data_cb_t *packet_consumer;

char buffer[MAX_PACKET_LEN];

ssize_t packet_received(const void *buffer, size_t len)
{
    (void)buffer;
    (void)len;

    // dump message to stdout
    printf("Received packet:\n");
    od_hex_dump(buffer, len < 128 ? len : 128, 0);
#ifdef BOARD_LORA3A_SENSOR1
    // parse command
#else
#ifdef BOARD_LORA3A_DONGLE
    // send a new command
#endif
#endif
    return 0;
}

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
    // SEND PAYLOAD
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
    to_lora(buffer, strlen(buffer));
    // WAIT 1 SECOND FOR COMMANDS
    lora_listen();
    ztimer_sleep(ZTIMER_MSEC, 1000);
    // ENTER BACKUP MODE
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
