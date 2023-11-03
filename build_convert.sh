#!/bin/sh

gcc source/vgm_convert/vgm_convert.c \
    source/vgm_convert/vgm_read.c \
    -o vgm_convert -lz

gcc source/vgm_convert/vgm_convert_fm.c \
    source/vgm_convert/vgm_read.c \
    -o vgm_convert_fm -lz
