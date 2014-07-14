#
# Makefile for miscellaneous I2C chip drivers.
#
# Do not add new drivers to this directory! It is DEPRECATED.
#
# Device drivers are better grouped according to the functionality they
# implement rather than to the bus they are connected to. In particular:
# * Hardware monitoring chip drivers go to drivers/hwmon
# * RTC chip drivers go to drivers/rtc
# * I/O expander drivers go to drivers/gpio
#

obj-$(CONFIG_SENSORS_AKM8976)	+= akm8976.o
obj-$(CONFIG_SENSORS_MT9T013)	+= mt9t013.o
obj-$(CONFIG_MOT_FEAT_LP3907)   += lp3907_i2c.o

ifeq (y, $(findstring y, $(CONFIG_MACH_CALGARY) $(CONFIG_MACH_MOT)))
obj-$(CONFIG_ADP5588_KEYPAD)    += adp5588_keypad.o
obj-$(CONFIG_SENSORS_LIS331DLH) += stmicro/
obj-$(CONFIG_SENSORS_AK8973)     += akm8973.o
endif

ifeq ($(CONFIG_MACH_MOT),y)
obj-$(CONFIG_FUEL_GAUGE_BQ27505) += bq27505.o
endif

ifeq ($(CONFIG_I2C_DEBUG_CHIP),y)
EXTRA_CFLAGS += -DDEBUG
endif
