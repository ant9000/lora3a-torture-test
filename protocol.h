#ifndef PROTOCOL_H
#define PROTOCOL_H

void from_lora(const char *buffer, size_t len, uint8_t *rssi, int8_t *snr);
void to_lora(const char *buffer, size_t len);

#endif /* PROTOCOL_H */
