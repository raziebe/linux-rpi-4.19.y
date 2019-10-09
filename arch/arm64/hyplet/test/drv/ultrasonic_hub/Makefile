TARGET= ultrasonicnet
OBJS  = ultrasonic_net.o

KDIR=/opt/raspberry/64bit/O/
PWD = $(shell pwd) 
CC=/opt/gcc-linaro-4.9-2015.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-gcc
LD=/opt/gcc-linaro-4.9-2015.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-ld
CROSS_COMPILE=/opt/gcc-linaro-4.9-2015.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-

obj-m      := $(TARGET).o
$(TARGET)-objs   := $(OBJS)

default:
	make -C $(KDIR) SUBDIRS=$(PWD) modules ARCH=arm64 
	
$(TARGET).o: $(OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ $(OBJS)
clean:
	-rm -rf .tmp_versions *.o .*.o.cmd .*.ko.cmd *.[ch]~ Modules.symvers *.order *.*.ko *.ko *.mod.c .*.c  Module.symvers
indent:
	indent -kr -i8 *.[ch]
