
/* TODO: Update to correct value? */
#define F_CPU 7160000UL

#include <stdbool.h>
#include <stdint.h>

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#define TONE_0_BIT      0x01
#define TONE_1_BIT      0x02
#define TONE_2_BIT      0x04
#define NOISE_BIT       0x08
#define VOLUME_0_BIT    0x10
#define VOLUME_1_BIT    0x20
#define VOLUME_2_BIT    0x40
#define VOLUME_3_BIT    0x80

// #include "../aqua_lake.h"
// #include "../bridge_zone.h"
// #include "../chocolate.h"
// #include "../louie_louie.h"
// #include "../reed_flutes.h"
// #include "../sky_high.h"
// #include "../tiny_cavern.h"
// #include "../turkish_march.h"

#include "../fm_jungle.h"

#if 0
static uint16_t outer_index = 0; /* Index into the compressed index_data */
static uint16_t inner_index = 0; /* Index when expanding references into index_data */
static uint16_t frame_index = 0; /* Index into frame data */
#endif
static uint16_t fm_index = 0;


/*
 * Write one byte of data to the sn76489.
 * PB0 = ~WE
 * PB1 = Ready
 */
static void psg_write (uint8_t data)
{
    PORTD = data;

    PORTB &= ~(1 << PB0); /* Unset bit 0, ~WE */

    /* Wait for ready signal to go low */
    while ((PINB & (1 << PB1)) == (1 << PB1));

    /* Wait for ready signal to go high */
    while ((PINB & (1 << PB1)) == 0);

    /* De-assert ~WE */
    PORTB |= (1 << PB0);
}


/*
 * Write one register to the ym2413.
 * PB2 = A0
 * PB4 = ~CS
 */
static void ym2413_write (uint8_t addr, uint8_t data)
{
    /* Prepare the address at least 10 ns before driving CS low. */
    PORTB &= ~(1 << PB2);   /* A0 = LOW */
    PORTD = addr;
    _delay_us(10);

    PORTB &= ~(1 << PB4);   /* CS = LOW */
    _delay_us(10);           /* CS must be held low for at least 80 ns */
    PORTB |= (1 << PB4);    /* CS = HIGH */

    /* Data needs to be held for 25 ns.
     * The ym2413 also needs 12 cycles before accepting the next write. */
    _delay_us(10);

    /* Prepare the data at least 10 ns before driving CS low. */
    PORTB |= (1 << PB2);    /* A0 = HIGH */
    PORTD = data;
    _delay_us(10);

    /* Write the data */
    PORTB &= ~(1 << PB4);   /* CS = LOW */
    _delay_us(10);           /* CS must be held low for at least 80 ns */
    PORTB |= (1 << PB4);    /* CS = HIGH */

    /* Data needs to be held for 25 ns.
     * The ym2413 also needs 84 cycles before accepting the next write. */
    _delay_us(10);
}


/* TODO: Temporarily disabling PSG code to test YM2413 */
#if 0

/* Flag for 'is the next nibble to the high nibble of its byte?' */
static bool nibble_high = false;

/*
 * Read the next nibble from the frame data.
 */
static uint8_t nibble_read ()
{
    if (nibble_high)
    {
        nibble_high = false;
        return  pgm_read_byte (&(frame_data[frame_index++])) >> 4;
    }
    else
    {
        nibble_high = true;
        return  pgm_read_byte (&(frame_data[frame_index])) & 0x0f;
    }
}


/*
 * Advance the index if we end half way through a byte.
 */
static void nibble_done ()
{
    if (nibble_high)
    {
        nibble_high = false;
        frame_index++;
    }
}


/*
 * Update the LEDs on PC{2..5}
 */
static void led_update (uint8_t channel, uint8_t data)
{
    static uint8_t led_data = 0;
    const uint8_t threshold = 0x08;
    channel += 2;


    if (data <= threshold)
    {
        led_data |= (1 << channel);
    }
    else
    {
        led_data &= ~(1 << channel);
    }

    PORTC = led_data;
}


/*
 * Called every 1/60s to apply the next set of register writes.
 */
static void tick ()
{
    static uint8_t delay = 0;
    static uint16_t segment_end = 0;

    /* Read and process the next frame */
    if (delay == 0)
    {
        uint16_t element;
        uint8_t frame;
        uint8_t data;

        /* If we are not already processing a segment of referenced
         * data, read a new element from the compressed index_data */
        if (inner_index == segment_end)
        {
            element = pgm_read_word (&(index_data[outer_index++]));

            if (element & 0x8000)
            {
                /* Segment */
                inner_index = element & 0x0fff;
                segment_end = inner_index + ((element >> 12) & 0x0007) + 2;
            }
            else
            {
                /* Single index */
                inner_index = outer_index - 1;
                segment_end = outer_index;
            }
        }

        /* Read the delay and frame_index from the index_data */
        frame_index = pgm_read_word (&(index_data[inner_index++]));
        delay = ((frame_index >> 12) & 0x0007) + 1;
        frame_index &= 0x0fff;

        /* Read the frame header from the frame_data */
        frame = pgm_read_byte (&(frame_data[frame_index++]));

        if (frame & TONE_0_BIT)
        {
            data = nibble_read ();
            psg_write (0x80 | 0x00 | data);

            data = nibble_read ();
            data |= nibble_read () << 4;
            psg_write (data);
        }
        if (frame & TONE_1_BIT)
        {
            data = nibble_read ();
            psg_write (0x80 | 0x20 | data);

            data = nibble_read ();
            data |= nibble_read () << 4;
            psg_write (data);
        }
        if (frame & TONE_2_BIT)
        {
            data = nibble_read ();
            psg_write (0x80 | 0x40 | data);

            data = nibble_read ();
            data |= nibble_read () << 4;
            psg_write (data);
        }
        if (frame & NOISE_BIT)
        {
            data = nibble_read ();
            psg_write (0x80 | 0x60 | data);
        }
        if (frame & VOLUME_0_BIT)
        {
            data = nibble_read ();
            psg_write (0x80 | 0x10 | data);
            led_update (0, data);
        }
        if (frame & VOLUME_1_BIT)
        {
            data = nibble_read ();
            psg_write (0x80 | 0x30 | data);
            led_update (1, data);
        }
        if (frame & VOLUME_2_BIT)
        {
            data = nibble_read ();
            psg_write (0x80 | 0x50 | data);
            led_update (2, data);
        }
        if (frame & VOLUME_3_BIT)
        {
            data = nibble_read ();
            psg_write (0x80 | 0x70 | data);
            led_update (3, data);
        }

        nibble_done ();
    }

    /* Check for end of data and loop */
    if (outer_index == END_FRAME_INDEX)
    {
        outer_index = LOOP_FRAME_INDEX_OUTER;
        inner_index = LOOP_FRAME_INDEX_INNER;
        segment_end = LOOP_FRAME_SEGMENT_END;
    }

    /* Decrement the delay counter */
    if (delay > 0)
    {
        delay--;
    }
}
#endif


/*
 * Update the LEDs on PC{5..2}
 */
static void led_show_int (uint8_t data)
{
    PORTC = (data & 0x0f) << 2;
}


/*
 * Called every 1/60s to apply the next set of register writes.
 */
static void fm_tick ()
{
    static uint8_t delay = 0;
    uint16_t element = 0;
    uint8_t element_type = 0;
    static uint8_t led_frame_count = 0;

    /* Read and process the next frame */
    if (delay == 0)
    {
        led_frame_count++;
        led_show_int (led_frame_count);

        do
        {
            /* The topmost two bits define the element type:
             *   0: Data element with 1/60s delay
             *   1: Data element with 2/60s delay
             *   2: Delay-only element
             *   3: Data element, another element follows */
            element = pgm_read_word (&(fm_data[fm_index++]));
            element_type = element >> 14;

            /* Register-write element */
            if (element_type != 2)
            {
                uint8_t addr = ((element >> 8) & 0x003f);
                uint8_t data = element & 0x00ff;

                ym2413_write (addr, data);

                /* Handle embedded delay */
                if (element_type == 0)
                {
                    delay = 1;
                }
                else if (element_type == 1)
                {
                    delay = 2;
                }
            }
            /* Delay-only element */
            else
            {
                delay = element & 0x00ff;
            }

        } while (element_type == 3);
    }

    /* Decrement the delay counter */
    if (delay > 0)
    {
        delay--;
    }

    /* Loop */
    if (fm_index == FM_LOOP_END)
    {
        fm_index = FM_LOOP_FRAME_INDEX;
    }
}

/*
 * 60 Hz interrupt.
 */
ISR (TIMER1_COMPA_vect)
{
    /* tick (); */
    fm_tick ();
}


/*
 * Entry point.
 *
 * PortB.0 = Write
 * PortB.1 = SN76489 Ready
 * PortB.2 = YM2413 A0
 * PortB.3 = Clock
 * PortB.4 = YM2413 CS
 * PortB.5 = YM2413 RESET
 *
 * PortC.2 = LED
 * PortC.3 = LED
 * PortC.4 = LED
 * PortC.5 = LED
 *
 * PortD = Data
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
    DDRB |= (1 << DDB0) | (1 << DDB2) | (1 << DDB3) | (1 << DDB4) | (1 << DDB5); /* Enable output */
    PORTB = (1 << PB0) | (1 << PB4); /* Active-low write signals set high to avoid accidental writes */

    /* Configure PortC as output for LEDs */
    DDRC = 0x3c;
    PORTC = 0;

    /* Configure PortD as output for data */
    DDRD = 0xff;
    PORTD = 0;

    /* Default register values */
    psg_write (0x80 | 0x1f); /* Mute Tone0 */
    psg_write (0x80 | 0x3f); /* Mute Tone1 */
    psg_write (0x80 | 0x5f); /* Mute Tone2 */
    psg_write (0x80 | 0x7f); /* Mute Noise */

    /* Use timer 1 to generate a 60 Hz interrupt */
    TCCR1A = 0;
    TCCR1B = (1 << WGM12) | (1 << CS11); /* CTC mode, pre-scale clock by 8 */
    OCR1A = 14914; /* Top value for counter, to give ~60 Hz */
    TIMSK = (1 << OCIE1A); /* Interrupt on Output-compare-A match */

    /* Wait 10ms and then take the ym2413 out of reset */
    _delay_ms (10);
    PORTB |= (1 << DDB5);
    _delay_ms (10);

    /* Enable interrupts */
    sei ();

    while (true)
    {
        _delay_ms (10);
    }
}
