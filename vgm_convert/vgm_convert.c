#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

const uint8_t  vgm_magic [4] = { 'V', 'g', 'm', ' ' };
const uint8_t gzip_magic [3] = { 0x1f, 0x8b, 0x08 };

#define SOURCE_SIZE_MAX 524288      /* 512 KiB */
#define OUTPUT_SIZE_MAX  32768      /*  32 KiB */

uint8_t *read_vgz (char *filename)
{
    gzFile source_vgz = NULL;
    uint8_t file_magic [4] = { 0 };
    uint8_t scratch [128] = { 0 };
    uint8_t *buffer = NULL;
    uint32_t filesize = 0;

    source_vgz = gzopen (filename, "rb");
    if (source_vgz == NULL)
    {
        fprintf (stderr, "Error: Unable to open vgz %s.\n", filename);
        return NULL;
    }

    gzread (source_vgz, file_magic, 4);
    gzrewind (source_vgz);

    /* Check the magic bytes are valid */
    if (memcmp (file_magic, vgm_magic, 4) != 0)
    {
        fprintf (stderr, "Error: File is not a valid VGM.\n");
        gzclose (source_vgz);
        return NULL;
    }

    /* Get the uncompressed filesize by reading the contents */
    while (gzread (source_vgz, scratch, 128) == 128);
    filesize = gztell (source_vgz);
    gzrewind (source_vgz);

    if (filesize > SOURCE_SIZE_MAX)
    {
        fprintf (stderr, "Error: Source file (uncompressed) larger than 512 KiB.\n");
        gzclose (source_vgz);
        return NULL;
    }

    /* Allocate a buffer */
    buffer = malloc (filesize);
    if (buffer == NULL)
    {
        fprintf (stderr, "Error: Unable to allocate %d bytes of memory.\n", filesize);
        gzclose (source_vgz);
        return NULL;
    }

    /* Read the file */
    if (gzread (source_vgz, buffer, filesize) != filesize)
    {
        fprintf (stderr, "Error: Unable to read %d bytes from file.\n", filesize);
        gzclose (source_vgz);
        free (buffer);
        return NULL;
    }

    gzclose (source_vgz);

    return buffer;
}

uint8_t *read_vgm (char *filename)
{
    FILE *source_vgm = NULL;
    uint8_t file_magic [4] = { 0 };
    uint8_t *buffer = NULL;
    uint32_t filesize = 0;

    source_vgm = fopen (filename, "rb");
    if (source_vgm == NULL)
    {
        fprintf (stderr, "Error: Unable to open %s.\n", filename);
        return NULL;
    }

    fread (file_magic, sizeof (uint8_t), 4, source_vgm);
    rewind (source_vgm);

    /* First, check if we should be using the vgz path instead */
    if (memcmp (file_magic, gzip_magic, 3) == 0)
    {
        fclose (source_vgm);

        return read_vgz (filename);
    }

    if (memcmp (file_magic, vgm_magic, 4) != 0)
    {
        fclose (source_vgm);
        fprintf (stderr, "Error: File is not a valid VGM.\n");
        return NULL;
    }

    /* Get the filesize */
    fseek (source_vgm, 0, SEEK_END);
    filesize = ftell (source_vgm);

    rewind (source_vgm);

    if (filesize > SOURCE_SIZE_MAX)
    {
        fprintf (stderr, "Error: Source file larger than 512 KiB.\n");
        fclose (source_vgm);
        return NULL;
    }

    /* Allocate a buffer */
    buffer = malloc (filesize);
    if (buffer == NULL)
    {
        fprintf (stderr, "Error: Unable to allocate %d bytes of memory.\n", filesize);
        fclose (source_vgm);
        return NULL;
    }

    /* Read the file */
    if (fread (buffer, sizeof (uint8_t), filesize, source_vgm) != filesize)
    {
        fprintf (stderr, "Error: Unable to read %d bytes from file.\n", filesize);
        fclose (source_vgm);
        free (buffer);
        return NULL;
    }

    fclose (source_vgm);

    return buffer;
}

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
static psg_regs current_state = { 0 };
static uint32_t samples_delay = 0;

/* Unique frames. Note that:
 *  1. Frames are variable length.
 *  2. A zero-frame is pre-populated at the start for use with delay-only indexes. */
static uint8_t  frame_data [OUTPUT_SIZE_MAX + 10] = { 0 };
static uint32_t frame_data_size = 1;

/* Index of each unique frame to speed up matching. */
static uint16_t frame_indexes [OUTPUT_SIZE_MAX + 10] = { 0 };
static uint16_t frame_count = 1;

/* Indexes into frame data to be used for playback. */
/* Note: two bytes per index is pretty big, we probably need ~12 bits.
 *       Consider:
 *        - nibble-packing.
 *        - Storing delay in the extra bits. */
static uint16_t index_data [OUTPUT_SIZE_MAX + 10] = { 0 };
static uint16_t index_data_count = 0;
static uint16_t loop_frame_index = 0;

#define TOTAL_SIZE (frame_data_size + index_data_count * 2)

/* Holding space for newly generated frame */
#define FRAME_SIZE_MAX 8
static uint8_t new_frame [FRAME_SIZE_MAX] = { 0 };

/* TODO: For PAL music, perhaps define delay as multiples of 1/50, or have
 *       a shorter delay like 1/300 that can cleanly describe both PAL and
 *       NTSC timings. */

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
 */
void write_frame (void)
{
    uint16_t index = 0xffff;
    uint16_t new_frame_size = generate_frame ();
    uint16_t frame_delay = samples_delay / 735;
    samples_delay -= frame_delay * 735;

    /* Check if the frame already exists */
    for (int i = 0; i < frame_count; i++)
    {
        if (memcmp (new_frame, &(frame_data [frame_indexes [i]]), new_frame_size) == 0)
        {
            /* Found - Output the index and return */
            /* TODO: Extra delays will be lost, so should be handled here
             *       instead of emitted by generate_frame. Remember to use
             *       0000 for 1/60, as 0/60 shouldn't happen.
             *       Also consider a different split, such as 3.13 instead
             *       of 4.12 if we need >4K of unique frames.
             *       */
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

    if (frame_delay <= 16)
    {
        uint16_t delay_bits = (frame_delay - 1) << 12;
        index_data [index_data_count++] = delay_bits | index;
    }
    else
    {
        /* More than 16/60s delay requires multiple indexes */
        index_data [index_data_count++] = 0xf000 | index;
        frame_delay -= 16;

        while (frame_delay)
        {
            if (frame_delay <= 16)
            {
                uint16_t delay_bits = (frame_delay - 1) << 12;
                index_data [index_data_count++] = delay_bits;
                frame_delay = 0;
            }
            else
            {
                index_data [index_data_count++] = 0xf000;
                frame_delay -= 16;
            }
        }
    }
}

int main (int argc, char **argv)
{
    /* File I/O */
    char *filename = argv [1];
    uint8_t *buffer = NULL;
    uint32_t vgm_offset = 0;

    /* PSG */
    uint8_t latch = 0;
    uint8_t data = 0;
    uint16_t data_low = 0;
    uint16_t data_high = 0;

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


    for (uint32_t i = vgm_offset; (i < SOURCE_SIZE_MAX) && (TOTAL_SIZE < OUTPUT_SIZE_MAX); i++)
    {
        if (i == loop_offset)
        {
            loop_frame_index = index_data_count;
            fprintf (stderr, "Loop frame index: %d.\n", loop_frame_index);
        }

        switch (buffer[i])
        {
        case 0x4f:
            i++; /* Gamegear stereo data - Ignore */
            break;

        case 0x50: /* PSG Data */
            if (samples_delay >= 735)
            {
                write_frame ();
            }
            data = buffer[++i];
            data_low  = data & 0x0f;
            data_high = data << 0x04;

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
            break;

        case 0x61: /* Wait n 44.1 KHz samples */
            samples_delay += * (uint16_t *)(&buffer [i+1]);
            i += 2;
            break;

        case 0x62: /* Wait 1/60 of a second */
            samples_delay += 735;
            break;

        case 0x63: /* Wait 1/50 of a second */
            samples_delay += 882;
            break;

        case 0x66: /* End of sound data */
            write_frame ();
            i = SOURCE_SIZE_MAX;
            break;

        /* 0x7n: Wait n+1 samples */
        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7a: case 0x7b:
        case 0x7c: case 0x7d: case 0x7e: case 0x7f:
            samples_delay += 1 + (buffer [i] & 0x0f);
            break;

        default:
            fprintf (stderr, "Unknown command %02x.\n", buffer [i]);
            break;
        }
    }

    if (TOTAL_SIZE >= (8192 - 640))
    {
        fprintf (stderr, "Warning: Output size %d.%02d KiB may not fit on ATMEGA-8.\n",
                 TOTAL_SIZE / 1024, (TOTAL_SIZE % 1024) * 100 / 1024);
    }

    printf ("#define LOOP_FRAME_INDEX %d\n", loop_frame_index);
    printf ("#define END_FRAME_INDEX %d\n\n", index_data_count);

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
    for (int i = 0; i < index_data_count; i++)
    {
        if (i % 8 == 0)
        {
            printf ("    ");
        }
        printf ("0x%04x%s", index_data [i], i == (index_data_count - 1) ? "\n" : ",");
        if (i == (index_data_count - 1))
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
    fprintf (stderr, " - %d bytes of index data.\n", index_data_count * 2);
    fprintf (stderr, " - %d bytes total.\n", TOTAL_SIZE);

    free (buffer);
}
