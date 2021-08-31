#ifndef COMMON_H
#define COMMON_H

#include <inttypes.h>
#include "ztimer.h"
#include "net/lora.h"
#include "sx127x.h"

#include "lora.h"

#define MAX_PAYLOAD_LEN 64
#define MAX_PACKET_LEN  (MAX_PAYLOAD_LEN + 10) // TODO: define Embit header

#define DEFAULT_LORA_BANDWIDTH        LORA_BW_500_KHZ
#define DEFAULT_LORA_SPREADING_FACTOR LORA_SF7
#define DEFAULT_LORA_CODERATE         LORA_CR_4_5
#define DEFAULT_LORA_CHANNEL          SX127X_CHANNEL_DEFAULT
#define DEFAULT_LORA_POWER            SX127X_RADIO_TX_POWER

typedef ssize_t (consume_data_cb_t)(const void *buffer, size_t len);

#endif /* COMMON_H */
