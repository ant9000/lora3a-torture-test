#ifndef DAFFY_H
#define DAFFY_H
// DAFFY possible addresses: 0x38, 0x39, 0x3a, 0x3b, 0x3c, 'x3d, 0x3e, 0x3f
#define DAFFY_CONFIG_REG 	(0x03)
#define DAFFY_IN_REG		(0x00)
#define DAFFY_OUT_REG		(0x01)

uint8_t probeDaffy(uint8_t address);
uint8_t initDaffy(uint8_t address);
uint8_t readDaffy(uint8_t address, uint8_t *inDaffy);
uint8_t writeDaffy(uint8_t address, uint8_t outDaffy);

#endif /* DAFFY_H */
