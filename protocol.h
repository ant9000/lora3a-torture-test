#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "net/netdev.h"

void from_lora(const char *buffer, size_t len, uint8_t *rssi, int8_t *snr);
void to_lora(const embit_header_t *header, const char *buffer, const size_t len);

#endif /* PROTOCOL_H */
