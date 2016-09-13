# Kernel Module to handle the GPIO setbits/clear bits via ioctl calls

EXTRA_CFLAGS+=-DADAFRUIT_RGBMATRIX_HAT
# turn this on to use the GPIO Sysfs interface to GPIO
#EXTRA_CFLAGS+=-DGPIO_SYSFS
# Turn this on if there is no BCM GPIO Driver and we can request/release the gpio mem region
#EXTRA_CFLAGS+=-DBCMGPIO_NOT

obj-m += leddriver.o
leddriver-objs  := leddrvr.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
