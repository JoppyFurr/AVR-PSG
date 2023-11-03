# AVR-PSG

Software for interfacing the ATMEGA-8 with the SN76489 and YM2413.

There are currently three modes that can be used:

### Streaming over UART

Send music into the RXD pin (PortD.0) at 28800 baud.
The format is explained in code comments.

* Use main.c
  * Make sure UART_BUILD is defined
  * Make sure EMBED_BUILD is not defined


A utility is included for playing back .vgm files:

```
./build_uart_play.sh
./vgm_uart_play my_tune.vgm
```

### Embedding SN76489 Music

So long as the size is not too great, a piece of music
can be included in the ATMEGA-8 flash. A simple compression
method is used to save space when repeated sequences occur.

* Use main.c
  * Make sure EMBED_BUILD is defined
  * Make sure UART_BUILD is not defined

The music is added to main.c as a header file. This file
can be generated with the 'vgm_convert` tool:

```
./vgm_convert my_tune.vgm > my_tune.h
./build.sh
```

Remember to update `main.c` to include the generated header file.

### Embedding YM2413 Music

Initial work has been done to add support for embedding YM2413
music. However, this has not yet been combined into the main
tools. Instead, there is a separate `main_fm.c` and `vgm_convert_fm.c`.

Embedding both FM and PSG sound is not yet supported, and
compression has not yet been implemented for FM.




Developed in Ōtautahi, Aotearoa.

