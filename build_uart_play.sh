#!/bin/sh

gcc source/vgm_uart_play.c \
    source/vgm_convert/vgm_read.c \
    -o vgm_uart_play -lz
