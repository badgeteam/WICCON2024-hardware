PREFIX ?= riscv64-elf
CH32V003FUN ?= ch32v003fun
MINICHLINK ?= minichlink

all : flash

TARGET ?= main
CFLAGS+=-O2
#ADDITIONAL_C_FILES+=

include $(CH32V003FUN)/ch32v003fun.mk

flash : cv_flash
	$(MINICHLINK)/minichlink -D
clean : cv_clean

