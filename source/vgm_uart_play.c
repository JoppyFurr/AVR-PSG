#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <asm/termbits.h>
#include <sys/ioctl.h>

#include "vgm_convert/vgm_read.h"

/* State tracking */
static uint32_t samples_delay = 0;
static int uart_fd = -1;

/* TODO: The combination of how handle_delay works
 *       and how we call a potentially blocking write
 *       to the UART mean that the music will play
 *       slightly too slow.
 *
 *       One option is to move uart writes to a
 *       separate thread. A better solution may be to
 *       update handle_delay so that it waits until
 *       a specific wall-time has been reached.
 */

/*
 * Wait the expected amount of time between register writes.
 */
void handle_delay (void)
{
    /* Convert 44.1 kHz samples into µs */
    usleep (samples_delay * 10000 / 441);
    samples_delay = 0;
}


/*
 * Write a byte to the UART.
 */
void uart_write (uint8_t data)
{
    int ret = write (uart_fd, &data, 1);

    if (ret != 1)
    {
        fprintf (stderr, "UART Write returns %d.\n", ret);
    }
}


/*
 * Stop sustained tones before exiting.
 */
void sigint_handler (int dummy)
{
    if (uart_fd >= 0)
    {
        uart_write (0x00);
        uart_write (0x01);
        exit (0);
    }
}


/*
 * Entry point.
 *
 * Currently contains the parser for VGM commands
 * and generator for the output text.
 */
int main (int argc, char **argv)
{
    /* File I/O */
    char *filename = argv [1];
    uint8_t *buffer = NULL;
    uint32_t vgm_offset = 0;

    /* Serial I/O */
    uart_fd = open ("/dev/ttyUSB0", O_RDWR);
    if (uart_fd < 0)
    {
        fprintf (stderr, "Cannot open ttyUSB0: %s.\n", strerror (errno));
        return EXIT_FAILURE;
    }

    struct termios2 uart_attributes;
    if (ioctl (uart_fd, TCGETS2, &uart_attributes) == -1)
    {
        fprintf (stderr, "Cannot get uart attributes: %s.\n", strerror (errno));
        return EXIT_FAILURE;
    }

    uart_attributes.c_cflag &= CSIZE;
    uart_attributes.c_cflag |= CS8;         /* 8 */
    uart_attributes.c_cflag &= ~PARENB;     /* N */
    uart_attributes.c_cflag &= ~CSTOPB;     /* 1 */
    uart_attributes.c_cflag &= ~CRTSCTS;    /* Disable flow-control */
    uart_attributes.c_cflag |= CREAD;       /* Enable reading */
    uart_attributes.c_cflag |= CLOCAL;      /* Ignore modem lines */
    uart_attributes.c_cflag &= ~CBAUD;
    uart_attributes.c_cflag |= CBAUDEX;    /* Use custom baud rate */

    uart_attributes.c_ispeed = 28800;       /* 28.8 k */
    uart_attributes.c_ospeed = 28800;

    uart_attributes.c_lflag &= ~ICANON; /* Disable canonical mode */
    uart_attributes.c_lflag &= ~(ECHO | ECHOE | ECHONL); /* Disable echo */
    uart_attributes.c_lflag &= ~ISIG;   /* Disable control characters */

    uart_attributes.c_iflag &= ~(IXON | IXOFF | IXANY); /* Disable software flow-control */
    uart_attributes.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
                                 INLCR |IGNCR | ICRNL); /* Disable handling of special bytes (Rx) */

    uart_attributes.c_oflag &= ~(OPOST | ONLCR); /* Disable handling of special bytes (Tx) */

    if (ioctl(uart_fd, TCSETS2, &uart_attributes) == -1)
    {
        fprintf (stderr, "Cannot set uart attributes: %s.\n", strerror (errno));
        return EXIT_FAILURE;
    }

    /* Send a zero to clear the command latch */
    uart_write (0x00);
    uart_write (0x01);
    usleep (100000);


    /* Set up signal handling to quiet the chips on exit */
    signal (SIGINT, sigint_handler);

    uint8_t command = 0;
    uint8_t addr = 0;
    uint8_t data = 0;

    if (argc != 2)
    {
        fprintf (stderr, "Error: No VGM file specified.\n");
        return EXIT_FAILURE;
    }

    buffer = read_vgm (filename);

    if (buffer == NULL)
    {
        /* read_vgm should already have output an error message */
        return EXIT_FAILURE;
    }

    fprintf (stderr, "Version: %x.\n",       * (uint32_t *)(&buffer [0x08]));
    fprintf (stderr, "Clock rate: %d Hz.\n", * (uint32_t *)(&buffer [0x0c]));
    fprintf (stderr, "Rate: %d Hz.\n",       * (uint32_t *)(&buffer [0x24]));
    fprintf (stderr, "VGM offset: %02x.\n",  * (uint32_t *)(&buffer [0x34]));

    uint32_t loop_offset = * (uint32_t *)(&buffer [0x1c]);
    if (loop_offset != 0)
    {
        loop_offset += 0x1c; /* Offsets in the VGM header are relative to their own position in the file */
    }

    fprintf (stderr, "Loop offset: %02x.\n",  * (uint32_t *)(&buffer [0x1c]));


    /* Note: We assume a little-endian host */
    if (* (uint32_t *)(&buffer [0x34]) != 0)
    {
        vgm_offset = 0x34 + * (uint32_t *)(&buffer [0x34]);
    }
    else
    {
        vgm_offset = 0x40;
    }

    uint32_t ym2413_count = 0;

    uint32_t i = vgm_offset;
    while (i < SOURCE_SIZE_MAX)
    {
        command = buffer[i++];

        switch (command)
        {
        case 0x4f:
            i++; /* Gamegear stereo data - Ignore */
            break;

        case 0x50: /* PSG Data */
            if (samples_delay)
            {
                handle_delay ();
            }
            data = buffer[i++];
            uart_write (0x40);
            uart_write (data);
            break;

        case 0x51: /* YM2413 */
            if (samples_delay)
            {
                handle_delay ();
            }
            addr = buffer[i++];
            data = buffer[i++];
            uart_write (0x80 | addr);
            uart_write (data);
            break;

        case 0x61: /* Wait n 44.1 KHz samples */
            samples_delay += * (uint16_t *)(&buffer [i]);
            i += 2;
            break;

        case 0x62: /* Wait 1/60 of a second */
            samples_delay += 735;
            break;

        case 0x63: /* Wait 1/50 of a second */
            samples_delay += 882;
            break;

        case 0x66: /* End of sound data - Loop */
            i = loop_offset;
            break;

        /* 0x7n: Wait n+1 samples */
        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7a: case 0x7b:
        case 0x7c: case 0x7d: case 0x7e: case 0x7f:
            samples_delay += 1 + (command & 0x0f);
            break;

        default:
            fprintf (stderr, "Unknown command %02x.\n", command);
            break;
        }
    }

    free (buffer);
}
