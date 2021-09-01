APPLICATION = lora3a-torture-test
BOARD ?= lora3a-sensor1
RIOTBASE ?= $(CURDIR)/../RIOT
EXTERNAL_BOARD_DIRS ?= $(CURDIR)/../lora3a-boards/boards
QUIET ?= 1
DEVELHELP ?= 1

USEMODULE += sx1276
USEMODULE += fmt
USEMODULE += od
USEMODULE += od_string
USEMODULE += printf_float
USEMODULE += saul_default
USEMODULE += periph_adc
USEMODULE += periph_cpuid
USEMODULE += periph_i2c
USEMODULE += periph_rtt
USEMODULE += periph_rtc_mem

include $(RIOTBASE)/Makefile.include
