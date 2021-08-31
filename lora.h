#ifndef LORA_H
#define LORA_H

typedef void (lora_data_cb_t)(const char *buffer, size_t len);

typedef struct {
    uint8_t bandwidth;
    uint8_t spreading_factor;
    uint8_t coderate;
    uint32_t channel;
    int16_t power;
    lora_data_cb_t *data_cb;
} lora_state_t;

int lora_init(const lora_state_t *state);
int lora_write(char *msg, size_t len);
void lora_listen(void);

#endif /* LORA_H */
