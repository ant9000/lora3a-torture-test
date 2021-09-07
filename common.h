#ifndef COMMON_H
#define COMMON_H

#include <inttypes.h>
#include "ztimer.h"
#include "net/lora.h"
#include "sx127x.h"

#include "lora.h"
#include "embit.h"

#define MAX_PAYLOAD_LEN 110
#define MAX_PACKET_LEN  (MAX_PAYLOAD_LEN + EMB_HEADER_LEN)

#define DEFAULT_LORA_BANDWIDTH        LORA_BW_500_KHZ
#define DEFAULT_LORA_SPREADING_FACTOR LORA_SF7
#define DEFAULT_LORA_CODERATE         LORA_CR_4_5
#define DEFAULT_LORA_CHANNEL          SX127X_CHANNEL_DEFAULT
#define DEFAULT_LORA_POWER            SX127X_RADIO_TX_POWER

typedef ssize_t (consume_data_cb_t)(const void *buffer, size_t len, uint8_t *rssi, int8_t *snr);

#endif /* COMMON_H */
