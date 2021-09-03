#include <stdio.h>
#include "fmt.h"
#include "od.h"

#include "mutex.h"

#define ENABLE_DEBUG 0
#include "debug.h"
#define HEXDUMP(msg, buffer, len) if (ENABLE_DEBUG) { puts(msg); od_hex_dump((char *)buffer, len, 0); }

#include "common.h"

extern consume_data_cb_t *packet_consumer;

static mutex_t lora_lock = MUTEX_INIT;

void lora_acquire(void)
{
    mutex_lock(&lora_lock);
}

void lora_release(void)
{
    mutex_unlock(&lora_lock);
}

void from_lora(const char *buffer, size_t len)
{
    lora_acquire();
    HEXDUMP("RECEIVED PACKET:", buffer, len);
    packet_consumer((char *)buffer, len);
    lora_release();
}

void to_lora(const char *buffer, size_t len)
{
    lora_acquire();
    HEXDUMP("SENDING PACKET:", buffer, len);
    lora_write((char *)buffer, len);
    lora_release();
}
