ccflags-y +=   -D__HYPLET__  -D__HYPLET_VM__   -Iarch/arm/include/ -D__LINUX_ARM_ARCH__=8
obj-$(CONFIG_HYPLET) += \
	hyplet_mmu.o \
	hypletS.o \
	hyplet.o \
	hyplet_gic.o \
	hyplet_rpc.o \
	hyp-init.o \
	hyplet_arm.o \
	hyplet_ipa.o \
	acqusition_trap.o \
	mmu.o
