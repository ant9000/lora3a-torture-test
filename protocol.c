#include <stdio.h>
#include "fmt.h"
#include "od.h"

#include "net/netdev.h"
#include "mutex.h"

#include "common.h"

extern consume_data_cb_t *packet_consumer;

mutex_t lora_write_lock = MUTEX_INIT;
mutex_t lora_read_lock = MUTEX_INIT;

void from_lora(const char *buffer, size_t len, uint8_t *rssi, int8_t *snr)
{
    mutex_lock(&lora_read_lock);
    packet_consumer((char *)buffer, len, rssi, snr);
    mutex_unlock(&lora_read_lock);
}

void to_lora(const iolist_t *packet)
{
    mutex_lock(&lora_write_lock);
    lora_write(packet);
    mutex_unlock(&lora_write_lock);
}
