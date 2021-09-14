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
    embit_header_t *header;
    void *payload;
    size_t n;
    mutex_lock(&lora_read_lock);
    header = (embit_header_t *)buffer;
    if ((header->signature == EMB_SIGNATURE) && (len > EMB_HEADER_LEN)) {
        payload = (char *)buffer + EMB_HEADER_LEN;
        n = len - EMB_HEADER_LEN;
        packet_consumer(header, payload, n, rssi, snr);
    }
    mutex_unlock(&lora_read_lock);
}

void to_lora(const embit_header_t *header, const char *buffer, const size_t len)
{
    iolist_t packet, payload;
    mutex_lock(&lora_write_lock);
    packet.iol_base = (void *)header;
    packet.iol_len = EMB_HEADER_LEN;
    packet.iol_next = &payload;
    payload.iol_base = (void *)buffer;
    payload.iol_len = len;
    payload.iol_next = NULL;
    lora_write(&packet);
    mutex_unlock(&lora_write_lock);
}
