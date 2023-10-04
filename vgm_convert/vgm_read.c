#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "vgm_read.h"

const uint8_t  vgm_magic [4] = { 'V', 'g', 'm', ' ' };
const uint8_t gzip_magic [3] = { 0x1f, 0x8b, 0x08 };


/*
 * Read a compressed .vgm file into an allocated buffer.
 * The buffer should be freed when no longer needed.
 */
static uint8_t *read_vgz (char *filename)
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


/*
 * Read a .vgm file into an allocated buffer.
 * The buffer should be freed when no longer needed.
 */
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
