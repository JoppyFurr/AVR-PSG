
/* TODO: Update to correct value? */
#define F_CPU 7160000UL

#include <stdbool.h>
#include <stdint.h>

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>


#define TONE_0_BIT     0x01
#define TONE_1_BIT     0x02
#define TONE_2_BIT     0x04
#define NOISE_BIT      0x08
#define VOLUME_0_1_BIT 0x10
#define VOLUME_2_N_BIT 0x20

#include "louie_louie.h"
// #include "sky_high.h"

void psg_write (uint8_t data)
{
    uint8_t port_b;

    PORTD = data;

    port_b = PORTB;
    port_b &= 0xfe; /* Unset bit 0, ~WE */
    PORTB = port_b;

    /* Wait for ready signal to go low */
    while ((PINB & 0x02) == 0x02);

    /* Wait for ready signal to go high */
    while ((PINB & 0x02) == 0x00);

    /* De-assert ~WE */
    PORTB |= 0x01;
}

void tick ()
{
    static uint8_t delay = 0;
    static uint16_t index = 0;

    /* Read and process the next frame */
    if (delay == 0)
    {
        uint8_t frame = pgm_read_byte (&(music_data[index++]));
        uint8_t data;

#if 0
        /* End of data - Restart */
        if (frame == 0x00)
        {
            delay = 120;
            index = 0;
        }
        else
        {
#endif
            if (frame & TONE_0_BIT)
            {
                data = pgm_read_byte (&(music_data[index++]));
                psg_write (0x80 | 0x00 | data);

                data = pgm_read_byte (&(music_data[index++]));
                psg_write (data);
            }
            if (frame & TONE_1_BIT)
            {
                data = pgm_read_byte (&(music_data[index++]));
                psg_write (0x80 | 0x20 | data);

                data = pgm_read_byte (&(music_data[index++]));
                psg_write (data);
            }
            if (frame & TONE_2_BIT)
            {
                data = pgm_read_byte (&(music_data[index++]));
                psg_write (0x80 | 0x40 | data);

                data = pgm_read_byte (&(music_data[index++]));
                psg_write (data);
            }
            if (frame & NOISE_BIT)
            {
                data = pgm_read_byte (&(music_data[index++]));
                psg_write (0x80 | 0x60 | data);
            }
            if (frame & VOLUME_0_1_BIT)
            {
                /* TODO: If we keep track of the volumes, then
                 *       only the changed value needs to be sent. */
                data = pgm_read_byte (&(music_data[index++]));
                psg_write (0x80 | 0x10 | (data & 0x0f));
                psg_write (0x80 | 0x30 | (data >> 4));
            }
            if (frame & VOLUME_2_N_BIT)
            {
                data = pgm_read_byte (&(music_data[index++]));
                psg_write (0x80 | 0x50 | (data & 0x0f));
                psg_write (0x80 | 0x70 | (data >> 4));
            }

            delay = (frame >> 6) + 1;
        }
#if 0
    }
#endif

    /* Decrement the delay counter */
    if (delay > 0)
    {
        delay--;
    }
}


/*
 * 60 Hz interrupt.
 */
ISR (TIMER1_COMPA_vect)
{
    tick ();
}


/*
 * Entry point.
 *
 * PortD = Data
 * PortB.0 = Write
 * PorbB.1 = Ready
 */
int main (void)
{
    _delay_ms (10);
    /* Set clock to 7.15909 MHz */
    /* TODO: Re-calibrate when there's a linear regulator in the circuit */
    OSCCAL = 0xaf; /* Gives a clock of ~7.16 MHz, depending on voltage */
    _delay_ms (10);

    /* Use timer 2 to generate the SN76489 clock (2.579 MHz) on the OC2 pin. (Pin 17, PB3) */
    TCCR2 = (1 << WGM21) | (1 << COM20) | (1 << CS20); /* CTC mode, toggle output-compare */
    OCR2 = 0; /* Reset the counter every increment */

    /* Enable output for clock and write-enable */
    DDRB |= (1 << DDB0) | (1 << DDB3); /* Enable output */

    /* Configure PortD as output for data */
    DDRD = 0xff;
    PORTD = 0;

    psg_write (0x80 | 0x3f); /* Mute Tone0 */
    psg_write (0x80 | 0x3f); /* Mute Tone1 */
    psg_write (0x80 | 0x5f); /* Mute Tone2 */
    psg_write (0x80 | 0x7f); /* Mute Noise */

    /* Use timer 1 to generate a 60 Hz interrupt */
    TCCR1A = 0;
    TCCR1B = (1 << WGM12) | (1 << CS11); /* CTC mode, pre-scale clock by 8 */
    OCR1A = 14914; /* Top value for counter, to give ~60 Hz */
    TIMSK = (1 << OCIE1A); /* Interrupt on Output-compare-A match */

    /* Enable interrupts */
    sei ();

    while (true)
    {
        _delay_ms (10);
    }
}
