#ifndef PROTOCOL_H
#define PROTOCOL_H

void lora_acquire(void);
void lora_release(void);
void from_lora(const char *buffer, size_t len);
void to_lora(const char *buffer, size_t len);

#endif /* PROTOCOL_H */
