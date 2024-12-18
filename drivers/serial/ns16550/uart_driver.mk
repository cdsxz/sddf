#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Include this snippet in your project Makefile to build
#    the ns16550 UART driver
# Requires uart_base to be set to the mapped address of the UART
#    registers in the system description file

UART_DRIVER_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

uart_driver.elf: serial/ns16650/uart_driver.o
	$(LD) $(LDFLAGS) $< $(LIBS) -o $@

serial/ns16650/uart_driver.o: ${UART_DRIVER_DIR}/uart.c |serial/ns16650
	$(CC) -c $(CFLAGS) -I${UART_DRIVER_DIR}/include -o $@ $< 

serial/ns16650:
	mkdir -p $@

-include serial/ns16650/uart_driver.d

clean::
	rm -f serial/ns16650/uart_driver.[do]
clobber:: clean
	rm -rf uart_driver.elf serial

