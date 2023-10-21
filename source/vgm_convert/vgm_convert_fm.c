#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vgm_read.h"

#define OUTPUT_SIZE_MAX  32768      /*  32 KiB */

/* A struct to represent the psg registers */
/* For now, just tones. Noise should be added later */
typedef struct psg_regs_s
{
    uint16_t tone_0; /* 10 bits */
    uint16_t tone_1; /* 10 bits */
    uint16_t tone_2; /* 10 bits */
    uint8_t noise;  /* 4 bits */
    uint8_t volume_0;
    uint8_t volume_1;
    uint8_t volume_2;
    uint8_t volume_3;
} psg_regs;

#define TONE_0_BIT      0x01
#define TONE_1_BIT      0x02
#define TONE_2_BIT      0x04
#define NOISE_BIT       0x08
#define VOLUME_0_BIT    0x10
#define VOLUME_1_BIT    0x20
#define VOLUME_2_BIT    0x40
#define VOLUME_3_BIT    0x80

/* State tracking */
static psg_regs current_state = { };
uint8_t ym2413_regs [0x40] = { };
static uint32_t samples_delay = 0;

/* Unique frames. Note that:
 *  1. Frames are variable length.
 *  2. A zero-frame is pre-populated at the start for use with delay-only indexes. */
static uint8_t  frame_data [OUTPUT_SIZE_MAX + 10] = { };
static uint32_t frame_data_size = 1;

/* Index of each unique frame to speed up matching. */
static uint16_t frame_indexes [OUTPUT_SIZE_MAX + 10] = { };
static uint16_t frame_count = 1;

/* Indexes into frame data to be used for playback. */
/* Note: two bytes per index is pretty big, we probably need ~12 bits.
 *       Consider:
 *        - nibble-packing.
 *        - Storing delay in the extra bits. */
static uint16_t index_data [OUTPUT_SIZE_MAX + 10] = { };
static uint16_t index_data_count = 0;
static uint16_t loop_frame_index = 0;

static uint16_t compressed_index_data [OUTPUT_SIZE_MAX + 10] = {};
static uint16_t compressed_index_data_count = 0;
static uint16_t loop_frame_index_outer = 0;
static uint16_t loop_frame_index_inner = 0;
static uint16_t loop_frame_segment_end = 0;

static uint16_t fm_data [OUTPUT_SIZE_MAX + 10] = { };
static uint16_t fm_data_count = 0;
static uint16_t fm_loop_frame_index = 0;

#define TOTAL_SIZE (frame_data_size + compressed_index_data_count * 2 + fm_data_count * 2)

/* Holding space for newly generated frame */
#define FRAME_SIZE_MAX 8
static uint8_t new_frame [FRAME_SIZE_MAX] = { 0 };

/* TODO: For PAL music, perhaps define delay as multiples of 1/50, or have
 *       a shorter delay like 1/300 that can cleanly describe both PAL and
 *       NTSC timings. */


/*
 * Convert a collection of register writes into a
 * nibble-packed format for the micro controller.
 */
uint16_t generate_frame (void)
{
    static psg_regs previous_state;

    uint8_t frame_size = 1;

    uint8_t nibble [16] = { 0 };
    uint8_t nibble_count = 0;

    /* Clear all bits for the new frame */
    memset (new_frame, 0, sizeof (new_frame));

    /* Frame format description:
     *
     *  Bitfields: vvvv nttt
     *
     *  nttt -> 0001: Tone0 nibbles follow (3)
     *          0010: Tone1 nibbles follow (3)
     *          0100: Tone2 nibbles follow (3)
     *          1000: Noise nibble follows
     *
     *          Nibbles are packed least-significant nibble first.
     *          Within an output bytes, the least-significant nibble comes first.
     *
     *
     *  vvvv -> 0001: Tone0 volume nibble follows
     *       -> 0010: Tone1 volume nibble follows
     *       -> 0100: Tone2 volume nibble follows
     *       -> 1000: Noise volume nibble follows
     *
     *  Bytes follow in the order they appear in the above list.
     *  Two bytes for the 10-bit tone registers.
     */

    /* Tone0 */
    if (current_state.tone_0 != previous_state.tone_0)
    {
        new_frame [0] |= TONE_0_BIT;
        nibble [nibble_count++] = (current_state.tone_0 & 0x00f);
        nibble [nibble_count++] = (current_state.tone_0 & 0x0f0) >> 4;
        nibble [nibble_count++] = (current_state.tone_0 & 0x300) >> 8;
    }

    /* Tone1 */
    if (current_state.tone_1 != previous_state.tone_1)
    {
        new_frame [0] |= TONE_1_BIT;
        nibble [nibble_count++] = (current_state.tone_1 & 0x00f);
        nibble [nibble_count++] = (current_state.tone_1 & 0x0f0) >> 4;
        nibble [nibble_count++] = (current_state.tone_1 & 0x300) >> 8;
    }

    /* Tone2 */
    if (current_state.tone_2 != previous_state.tone_2)
    {
        new_frame [0] |= TONE_2_BIT;
        nibble [nibble_count++] = (current_state.tone_2 & 0x00f);
        nibble [nibble_count++] = (current_state.tone_2 & 0x0f0) >> 4;
        nibble [nibble_count++] = (current_state.tone_2 & 0x300) >> 8;
    }

    /* Noise */
    if (current_state.noise != previous_state.noise)
    {
        new_frame [0] |= NOISE_BIT;
        nibble [nibble_count++] = current_state.noise & 0x0f;
    }

    /* Volume 0 */
    if (current_state.volume_0 != previous_state.volume_0)
    {
        new_frame [0] |= VOLUME_0_BIT;
        nibble [nibble_count++] = current_state.volume_0 & 0x0f;
    }

    /* Volume 1 */
    if (current_state.volume_1 != previous_state.volume_1)
    {
        new_frame [0] |= VOLUME_1_BIT;
        nibble [nibble_count++] = current_state.volume_1 & 0x0f;
    }

    /* Volume 2 */
    if (current_state.volume_2 != previous_state.volume_2)
    {
        new_frame [0] |= VOLUME_2_BIT;
        nibble [nibble_count++] = current_state.volume_2 & 0x0f;
    }

    /* Volume 3 */
    if (current_state.volume_3 != previous_state.volume_3)
    {
        new_frame [0] |= VOLUME_3_BIT;
        nibble [nibble_count++] = current_state.volume_3 & 0x0f;
    }

    /* Pack nibbles */
    /* TODO: Use C bitfields */
    for (int i = 0; i < nibble_count; i++)
    {
        if (i % 2 == 0)
        {
            /* Low nibble */
            new_frame [frame_size] = (nibble [i] & 0x0f);
        }
        else
        {
            /* High nibble */
            new_frame [frame_size++] |= (nibble [i] & 0x0f) << 4;
        }
    }

    /* If we have an odd number of nibbles, remember to increment the frame size */
    if (nibble_count % 2 == 1)
    {
        frame_size++;
    }

    memcpy (&previous_state, &current_state, sizeof (psg_regs));

    return frame_size;
}


/*
 * Adds a frame to the output buffers.
 *
 * If the frame is new, it is added both to frame_data and index_data.
 * If the frame is a duplicate, it is only added to index_data.
 *
 * Format:
 *  [15]     - Always output 0, reserved for use by compression
 *  [14..12] - Delay, 1/60 to 8/60s
 *  [11..0]  - Index into frame data
 */
void psg_write_frame (void)
{
    uint16_t index = 0xffff;
    uint16_t new_frame_size = generate_frame ();
    uint16_t frame_delay = samples_delay / 735;

    /* TODO: Can't have both touching the globals... */
#if 0
    samples_delay -= frame_delay * 735;
#endif

    /* Check if the frame already exists */
    for (int i = 0; i < frame_count; i++)
    {
        if (memcmp (new_frame, &(frame_data [frame_indexes [i]]), new_frame_size) == 0)
        {
            /* Found */
            index = frame_indexes [i];
            break;
        }
    }

    /* If a matching index was not found, then this is a new unique frame. */
    if (index == 0xffff)
    {
        /* Check there is space for a new frame, as we use 12 bits to index them */
        if (frame_data_size >= 0x0fff)
        {
            fprintf (stderr, "Warning: frame_data too large to index.\n");
        }

        index = frame_data_size;
        frame_indexes [frame_count++] = index;

        /* Add the new frame to the frame_data buffer */
        for (int i = 0; i < new_frame_size; i++)
        {
            frame_data [frame_data_size++] = new_frame[i];
        }
    }

    if (frame_delay <= 8)
    {
        uint16_t delay_bits = (frame_delay - 1) << 12;
        index_data [index_data_count++] = delay_bits | index;
    }
    else
    {
        /* More than 16/60s delay requires multiple indexes */
        index_data [index_data_count++] = 0x7000 | index;
        frame_delay -= 8;

        while (frame_delay)
        {
            if (frame_delay <= 8)
            {
                uint16_t delay_bits = (frame_delay - 1) << 12;
                index_data [index_data_count++] = delay_bits;
                frame_delay = 0;
            }
            else
            {
                index_data [index_data_count++] = 0x7000;
                frame_delay -= 8;
            }
        }
    }
}


/*
 * Find repeating segments within index_data and use
 * references to these to save space.
 *
 * Format:
 *  [15]     - If 1, this entry refers to a sequence of previous indexes.
 *  [14..12] - Length of matching sequence, 2-9 words.
 *  [11..0]  - Index into compressed data.
 */
void compress_indexes (void)
{
    uint16_t match_length = 0;

    /* Iterate over non-compressed data, adding it to the compressed data */
    for (uint32_t i = 0; i < index_data_count; i += match_length)
    {
        uint16_t longest_segment_index = 0;
        uint16_t longest_segment_length = 0;
        match_length = 0;

        /* Iterate over compressed data, finding the longest matching segment */
        for (uint32_t j = 0; j < compressed_index_data_count; j++)
        {
            /* Check the length of this match */
            for (uint32_t k = 0; i + k < index_data_count && j + k < compressed_index_data_count; k++)
            {
                if (compressed_index_data [j + k] == index_data [i + k])
                {
                    if (k + 1 > longest_segment_length)
                    {
                        longest_segment_index = j;
                        longest_segment_length = k + 1;
                    }
                }
                else
                {
                    break;
                }
            }
        }

        if (longest_segment_length >= 2)
        {
            /* Limit match length */
            if (longest_segment_length > 9)
            {
                longest_segment_length = 9;
            }

            /* Emit reference - 3 bits of length, 12 bits of index */
            compressed_index_data [compressed_index_data_count++] = 0x8000 | ((longest_segment_length - 2) << 12) | longest_segment_index;
            match_length = longest_segment_length;
        }
        else
        {
            /* Emit index */
            compressed_index_data [compressed_index_data_count++] = index_data [i];
            match_length = 1;
        }

        if (loop_frame_index_outer == 0 &&
            i + (match_length - 1) >= loop_frame_index)
        {
            /* Outer index points at the next compressed element to play after this segment */
            loop_frame_index_outer = compressed_index_data_count;

            /* Inner index points to the loop frame itself */
            if (longest_segment_length >= 2)
            {
                uint8_t depth = loop_frame_index - i;
                loop_frame_index_inner = longest_segment_index + depth;
                loop_frame_segment_end = longest_segment_index + match_length;
            }
            else
            {
                loop_frame_index_inner = loop_frame_index_outer - 1;
                loop_frame_segment_end = loop_frame_index_outer;
            }
            uint16_t depth = i + match_length - loop_frame_index;
        }
    }

    fprintf (stderr, "Compressed indexes: %d bytes (%d indexes).\n", compressed_index_data_count * 2, compressed_index_data_count);
}


/*
 * Process a PSG register write from the VGM file.
 */
void psg_register_write (uint8_t data)
{
    static uint8_t latch = 0;
    uint16_t data_low  = data & 0x0f;
    uint16_t data_high = data << 0x04;

    if (data & 0x80) { /* Latch + data-low (4-bits) */

        latch = data & 0x70;

        switch (latch)
        {
        /* Tone0 */
        case 0x00:
            current_state.tone_0 &= 0x3f0;
            current_state.tone_0 |= data_low;
            break;

        case 0x10:
            current_state.volume_0 = data_low;
            break;

        /* Tone1 */
        case 0x20:
            current_state.tone_1 &= 0x3f0;
            current_state.tone_1 |= data_low;
            break;

        case 0x30:
            current_state.volume_1 = data_low;
            break;

        /* Tone2 */
        case 0x40:
            current_state.tone_2 &= 0x3f0;
            current_state.tone_2 |= data_low;
            break;

        case 0x50:
            current_state.volume_2 = data_low;
            break;

        /* Noise */
        case 0x60:
            current_state.noise = data_low;
            break;

        case 0x70:
            current_state.volume_3 = data_low;
            break;
        }
    }
    else { /* Data-high */
        switch (latch)
        {
        /* Tone0 */
        case 0x00:
            current_state.tone_0 &= 0x00f;
            current_state.tone_0 |= data_high;
            break;

        case 0x10:
            current_state.volume_0 = data_low;
            break;

        /* Tone1 */
        case 0x20:
            current_state.tone_1 &= 0x00f;
            current_state.tone_1 |= data_high;
            break;

        case 0x30:
            current_state.volume_1 = data_low;
            break;

        /* Tone2 */
        case 0x40:

            current_state.tone_2 &= 0x00f;
            current_state.tone_2 |= data_high;
            break;

        case 0x50:
            current_state.volume_2 = data_low;
            break;

        /* Noise */
        case 0x60:
            current_state.noise = data_low;
            break;

        case 0x70:
            current_state.volume_3 = data_low;
            break;
        }
    }
}

/* YM2413 format:
 *
 * Two bytes per write
 *
 * Reg : Data
 *
 * Register addresses: 0-7, 10-18, 20-28, 30-38, 0e
 * 36 registers needs 6 bits, two msb are never used.
 *
 * Potentially:
 *  00 - End marker, delay = 1/60
 *  01 - End marker, delay = next byte
 *  10 - Another full register write follows
 *  11 - Another register write follows, assume the address increments by one
 *
 */

/*
 * Adds a frame to the output buffers.
 * Note: Temporary format, will be transitioned to frame+indexes.
 *
 * Format:
 *  [15..14] - 0: Frame ends, delay = 1/60
 *             1: Frame ends, delay = 2/60
 *             2: Frame ends, contains delay in data field.
 *             3: Frame continues
 *  [13..8]  - register address
 *  [7..0]   - register data
 */
void ym2413_write_frame (void)
{
    static uint8_t previous_ym2413_regs [0x40] = { };

    /* TODO: Can't have both touching the globals... */
    uint16_t frame_delay = samples_delay / 735;
    samples_delay -= frame_delay * 735;
    bool includes_writes = false;
    bool emit_delay_word = true;

    for (uint16_t addr = 0; addr < 0x40; addr++)
    {
        if (ym2413_regs [addr] != previous_ym2413_regs [addr])
        {
            /* Assume the frame continues and fix-up later */
            fm_data [fm_data_count++] = 0xc000 | (addr << 8) | ym2413_regs [addr];
            includes_writes = true;
        }
    }

    /* If the delay is small, it is included in the last write */
    if (frame_delay == 1 && includes_writes)
    {
        fm_data [fm_data_count - 1] &= 0x3fff;
        emit_delay_word = false;
    }
    else if (frame_delay == 2 && includes_writes)
    {
        fm_data [fm_data_count - 1] &= 0x3fff;
        fm_data [fm_data_count - 1] |= 0x4000;
        emit_delay_word = false;
    }

    if (emit_delay_word)
    {
        fm_data [fm_data_count++] = 0x8000 | (frame_delay & 0xff);
    }

    memcpy (&previous_ym2413_regs, ym2413_regs, sizeof (ym2413_regs));
}


/*
 *
 */
void ym2413_register_write (uint8_t addr, uint8_t value)
{
    if (addr >= 0x40)
    {
        fprintf (stderr, "ym2413: Ignoring high register address %02x.\n", addr);
        return;
    }

    ym2413_regs [addr] = value;
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
    while (buffer[i] != 0x66 && i < SOURCE_SIZE_MAX && TOTAL_SIZE < OUTPUT_SIZE_MAX)
    {
        command = buffer[i++];

        if (i == loop_offset)
        {
            loop_frame_index = index_data_count;
            fprintf (stderr, "Loop frame index: %d.\n", loop_frame_index);
            fm_loop_frame_index = fm_data_count;
            fprintf (stderr, "Loop frame index (fm): %d.\n", loop_frame_index);
        }

        switch (command)
        {
        case 0x4f:
            i++; /* Gamegear stereo data - Ignore */
            break;

        case 0x50: /* PSG Data */
            if (samples_delay >= 735)
            {
                psg_write_frame ();
            }
            data = buffer[i++];
            psg_register_write (data);
            break;

        case 0x51: /* YM2413 */
            if (samples_delay >= 735)
            {
                ym2413_write_frame ();
            }
            addr = buffer[i++];
            data = buffer[i++];
            ym2413_register_write (addr, data);
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

        case 0x66: /* End of sound data - should never be hit as this breaks the loop */
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

    /* Write final frames */
    psg_write_frame ();
    ym2413_write_frame ();

    compress_indexes ();

    if (TOTAL_SIZE >= (8192 - 724))
    {
        fprintf (stderr, "Warning: Output size %d.%02d KiB may not fit on ATMEGA-8.\n",
                 TOTAL_SIZE / 1024, (TOTAL_SIZE % 1024) * 100 / 1024);
    }

    /* TODO: Only emit structures if they are needed.
     *       Ie, leave psg structures out if FM-only. */

    printf ("#define LOOP_FRAME_INDEX_INNER %d\n", loop_frame_index_inner);
    printf ("#define LOOP_FRAME_INDEX_OUTER %d\n", loop_frame_index_outer);
    printf ("#define LOOP_FRAME_SEGMENT_END %d\n", loop_frame_segment_end);
    printf ("#define END_FRAME_INDEX %d\n\n", compressed_index_data_count);

    printf ("#define FM_LOOP_FRAME_INDEX %d\n", fm_loop_frame_index);
    printf ("#define FM_LOOP_END %d\n", fm_data_count);

    printf ("const uint8_t frame_data [] PROGMEM = {\n");
    for (int i = 0; i < frame_data_size; i++)
    {
        if (i % 16 == 0)
        {
            printf ("    ");
        }
        printf ("0x%02x%s", frame_data [i], i == (frame_data_size - 1) ? "\n" : ",");
        if (i == (frame_data_size - 1))
        {
            break;
        }
        if (i % 16 == 15)
        {
            printf ("\n");
        }
        else
        {
            printf (" ");
        }
    }
    printf ("};\n\n");

    printf ("const uint16_t index_data [] PROGMEM = {\n");
    for (int i = 0; i < compressed_index_data_count; i++)
    {
        if (i % 8 == 0)
        {
            printf ("    ");
        }
        printf ("0x%04x%s", compressed_index_data [i], i == (compressed_index_data_count - 1) ? "\n" : ",");
        if (i == (compressed_index_data_count - 1))
        {
            break;
        }
        if (i % 8 == 7)
        {
            printf ("\n");
        }
        else
        {
            printf (" ");
        }
    }
    printf ("};\n");

    printf ("const uint16_t fm_data [] PROGMEM = {\n");
    for (int i = 0; i < fm_data_count; i++)
    {
        if (i % 8 == 0)
        {
            printf ("    ");
        }
        printf ("0x%04x%s", fm_data [i], i == (fm_data_count - 1) ? "\n" : ",");
        if (i == (fm_data_count - 1))
        {
            break;
        }
        if (i % 8 == 7)
        {
            printf ("\n");
        }
        else
        {
            printf (" ");
        }
    }
    printf ("};\n");

    fprintf (stderr, "Done.\n");
    fprintf (stderr, " - %d bytes of frame data. (%d unique frames)\n", frame_data_size, frame_count);
    fprintf (stderr, " - %d bytes of index data.\n", compressed_index_data_count * 2);
    fprintf (stderr, " - %d bytes of fm data.\n", fm_data_count * 2);
    fprintf (stderr, " - %d bytes total.\n", TOTAL_SIZE);

    free (buffer);
}
