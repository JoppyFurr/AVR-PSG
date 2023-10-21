#!/bin/sh

# Exit on first error
set -e

# Note:  min-pagesize=0 is a work-around for a bug in GCC 12. Hopefully fixed in GCC 14?
avr-gcc -Os -Wall -mcall-prologues -mmcu=atmega8 --param=min-pagesize=0 source/main.c -o main.obj
avr-objcopy -R .eeprom -O ihex main.obj main.hex

if [ "$1" = "write" ]
then
    TTY="/dev/ttyUSB0"

    LFUSE="$(avrdude -p m8 -c avr910 -P ${TTY} -U lfuse:r:-:h 2>&1 | grep '^0x')"
    if [ "${LFUSE}" = 0xe4 ]
    then
        echo "L-fuse already set to 0xe4 (8 MHz)"
    else
        echo "Changing L-fuse value to 0xe4 (8 MHz)"
        avrdude -p m8 -c avr910 -P ${TTY} -U lfuse:w:0xe4:m
    fi
    
    echo "Writing main.hex..."
    avrdude -p m8 -c avr910 -P ${TTY} -U flash:w:main.hex
fi
