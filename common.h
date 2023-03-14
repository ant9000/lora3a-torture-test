#ifndef COMMON_H
#define COMMON_H

#include <inttypes.h>
#include "ztimer.h"
#include "net/lora.h"
#include "sx127x.h"

#include "lora.h"
#include "embit.h"

#define MAX_PAYLOAD_LEN 120
#ifndef AES
#define MAX_PACKET_LEN  (MAX_PAYLOAD_LEN + EMB_HEADER_LEN)
#else
#define MAX_PACKET_LEN  (MAX_PAYLOAD_LEN + EMB_HEADER_LEN + 12 + 16)
#endif

#ifndef CUSTOMER
/* ACME 
#define DEFAULT_LORA_BANDWIDTH        LORA_BW_500_KHZ
#define DEFAULT_LORA_SPREADING_FACTOR LORA_SF7
#define DEFAULT_LORA_CODERATE         LORA_CR_4_5
#define DEFAULT_LORA_CHANNEL          SX127X_CHANNEL_DEFAULT
#define DEFAULT_LORA_POWER            SX127X_RADIO_TX_POWER
*/
/* LABTEST */ 
#define DEFAULT_LORA_BANDWIDTH        LORA_BW_500_KHZ
#define DEFAULT_LORA_SPREADING_FACTOR LORA_SF7
#define DEFAULT_LORA_CODERATE         LORA_CR_4_5
#define DEFAULT_LORA_CHANNEL          (868000000UL)
#define DEFAULT_LORA_POWER            SX127X_RADIO_TX_POWER



/* FAIR 
#define DEFAULT_LORA_BANDWIDTH        LORA_BW_500_KHZ
#define DEFAULT_LORA_SPREADING_FACTOR LORA_SF7
#define DEFAULT_LORA_CODERATE         LORA_CR_4_5
#define DEFAULT_LORA_CHANNEL          (867000000UL)
#define DEFAULT_LORA_POWER            SX127X_RADIO_TX_POWER
*/
#else
/* CUSTOMER */  
#define DEFAULT_LORA_BANDWIDTH        LORA_BW_500_KHZ
#define DEFAULT_LORA_SPREADING_FACTOR LORA_SF7
#define DEFAULT_LORA_CODERATE         LORA_CR_4_5
#define DEFAULT_LORA_CHANNEL          (866000000UL)
#define DEFAULT_LORA_POWER            SX127X_RADIO_TX_POWER
#endif

typedef ssize_t (consume_data_cb_t)(const embit_packet_t *packet);

#endif /* COMMON_H */
