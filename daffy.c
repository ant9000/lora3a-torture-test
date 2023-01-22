#include <stdio.h>
#include <inttypes.h>

#include "ztimer.h"
#include "periph/i2c.h"
#include "periph/gpio.h"

#include "board.h"
#include "daffy.h"

uint8_t probeDaffy(uint8_t address) {
	uint8_t value;
	// try a read from reg 0 of Daffy to check if it is connected
    if (i2c_read_reg(I2C_DEV(1), address, DAFFY_IN_REG, &value, 0)) {
        printf("ERROR: probe Daffy 0x%02x ", address);
		return 1;
	}
	return 0;
}	

uint8_t initDaffy(uint8_t address) {
	if (i2c_write_reg(I2C_DEV(1), address, DAFFY_OUT_REG, 0x00, 0)) { // preset relays at off position
        puts("ERROR: init Daffy 1");
		return 1;
	}
	if (i2c_write_reg(I2C_DEV(1), address, DAFFY_CONFIG_REG, 0x0F, 0)) {; // config PCA9534 with 4 out (high) and 4 in (low)
        puts("ERROR: init Daffy 2");
		return 1;
	}
	return 0;
}

uint8_t readDaffy(uint8_t address, uint8_t *inDaffy) {
		// read status of Daffy, ins and outs
    if (i2c_read_reg(I2C_DEV(1), address, DAFFY_IN_REG, inDaffy, 0)) {
        puts("ERROR: read Daffy");
		return 1;
	}
	return 0;	
}
	
uint8_t writeDaffy(uint8_t address, uint8_t outDaffy) {
	if (i2c_write_reg(I2C_DEV(1), address, DAFFY_OUT_REG, outDaffy, 0)) { 
        puts("ERROR: write Daffy");
		return 1;
	}
	return 0;
}	
