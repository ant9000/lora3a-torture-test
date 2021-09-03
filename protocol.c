#include <stdio.h>
#include "fmt.h"
#include "od.h"

#include "mutex.h"

#define ENABLE_DEBUG 0
#include "debug.h"
#define HEXDUMP(msg, buffer, len) if (ENABLE_DEBUG) { puts(msg); od_hex_dump((char *)buffer, len, 0); }

#include "common.h"

extern consume_data_cb_t *packet_consumer;

mutex_t lora_write_lock = MUTEX_INIT;
mutex_t lora_read_lock = MUTEX_INIT;

void from_lora(const char *buffer, size_t len)
{
    mutex_lock(&lora_read_lock);
    HEXDUMP("RECEIVED PACKET:", buffer, len);
    packet_consumer((char *)buffer, len);
    mutex_unlock(&lora_read_lock);
}

void to_lora(const char *buffer, size_t len)
{
    mutex_lock(&lora_write_lock);
    HEXDUMP("SENDING PACKET:", buffer, len);
    lora_write((char *)buffer, len);
    mutex_unlock(&lora_write_lock);
}
