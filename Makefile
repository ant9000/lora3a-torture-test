APPLICATION = lora3a-torture-test
RIOTBASE ?= $(CURDIR)/../RIOT
EXTERNAL_BOARD_DIRS ?= $(CURDIR)/../lora3a-boards/boards
QUIET ?= 1
DEVELHELP ?= 1
BME688_ACME1 ?= 0
BME688_ACME2 ?= 0
H10RX ?= 0

ROLE ?= node
AES ?= 1

USEMODULE += sx1276
USEMODULE += fmt
USEMODULE += od
USEMODULE += od_string
USEMODULE += printf_float
USEMODULE += saul_default
USEMODULE += periph_adc
USEMODULE += periph_cpuid
USEMODULE += periph_hwrng
USEMODULE += periph_i2c
USEMODULE += periph_rtt
USEMODULE += periph_rtc_mem
USEMODULE += periph_spi_reconfigure
USEMODULE += ztimer_usec

ifeq ($(H10RX), 1)
  CFLAGS += -DH10RX
endif

ifeq ($(BOARD),lora3a-h10)
	ifeq ($(BME688_ACME1), 1)
	  USEMODULE += bme680_fp bme680_i2c
	  USEMODULE += periph_i2c_reconfigure
	  CFLAGS += -DBME688_ACME1=1 -DBME680_PARAM_I2C_DEV=1 -DBME680_PARAM_I2C_ADDR=0x76
	# # TODO:
	# # - bus is off at boot, we should not call drivers/saul/init_devs/auto_init_bme680.c
	# # - 11/9/22 now power acme sensor 1 is on at boot only if requested
	endif
	ifeq ($(BME688_ACME2), 1)
	  USEMODULE += bme680_fp bme680_i2c
	  USEMODULE += periph_i2c_reconfigure
	  CFLAGS += -DBME688_ACME2=1 -DBME680_PARAM_I2C_DEV=2 -DBME680_PARAM_I2C_ADDR=0x76
	# # TODO:
	# # - bus is off at boot, we should not call drivers/saul/init_devs/auto_init_bme680.c
	# # - 11/9/22 now power acme sensor 2 is on at boot only if requested
	endif
endif

ifeq ($(ROLE), node)
  BOARD ?= lora3a-sensor1
  VARIANT ?= BOARD_VARIANT_HARVEST8
  ADDRESS ?= 1
endif

ifeq ($(ROLE), gateway)
  BOARD ?= lora3a-dongle
  ADDRESS ?= 254
  ifeq ($(BOARD), lora3a-dongle)
    USEMODULE += stdio_cdc_acm    
    TERMDELAYDEPS := $(filter reset flash flash-only, $(MAKECMDGOALS))
    ifneq (,$(TERMDELAYDEPS))
      # By default, add 2 seconds delay before opening terminal: this is required
      # when opening the terminal right after flashing. In this case, the stdio
      # over USB needs some time after reset before being ready.
      TERM_DELAY ?= 2
      TERMDEPS += term-delay
    endif
  endif
endif

ifneq (,$(VARIANT))
  CFLAGS += -D$(VARIANT)
endif

ifneq (,$(ADDRESS))
  CFLAGS += -DEMB_ADDRESS=$(ADDRESS)
endif

ifeq (1,$(AES))
  CFLAGS += -DCONFIG_AES
  ifneq (,$(AES_KEY))
    CFLAGS += -DAES_KEY="\"$(AES_KEY)\""
  endif
endif

term-delay: $(TERMDELAYDEPS)
	sleep $(TERM_DELAY)

include $(RIOTBASE)/Makefile.include
