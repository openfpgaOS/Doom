/* i_save_migrate.c -- import old PocketDoom combined save files. */

#include "config.h"

#ifndef OF_PC

#include "doomtype.h"
#include "i_save.h"
#include "p_saveg.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEGACY_FILENAME        "legacy.sav"
#define LEGACY_SLOT_COUNT      6
#define LEGACY_SLOT_SIZE       0x10000
#define LEGACY_FILE_SIZE       (LEGACY_SLOT_COUNT * LEGACY_SLOT_SIZE)
#define LEGACY_PAD_SIZE        4
#define LEGACY_V1_HEADER_SIZE  8
#define LEGACY_V2_HEADER_SIZE  16
#define LEGACY_DECODE_SIZE     0x30000

#define SAVE_WRAP_HEADER_SIZE  16
#define SAVE_WRAP_VERSION      1
#define SAVE_WRAP_MAGIC_0      'P'
#define SAVE_WRAP_MAGIC_1      'D'
#define SAVE_WRAP_MAGIC_2      'S'
#define SAVE_WRAP_MAGIC_3      'V'

#define LZ_WINDOW              4096
#define LZ_MIN_MATCH           3

static uint32_t current_game_id;

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t) p[0]
        | ((uint32_t) p[1] << 8)
        | ((uint32_t) p[2] << 16)
        | ((uint32_t) p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t) value;
    p[1] = (uint8_t) (value >> 8);
    p[2] = (uint8_t) (value >> 16);
    p[3] = (uint8_t) (value >> 24);
}

static uint32_t fnv1a_update(uint32_t hash, const void *data, size_t length)
{
    const uint8_t *bytes = data;

    for (size_t i = 0; i < length; ++i)
    {
        hash ^= bytes[i];
        hash *= 0x01000193u;
    }

    return hash;
}

static uint32_t hash_file_identity(uint32_t hash, const char *filename)
{
    uint8_t header[12];
    FILE *fp;
    size_t got;

    if (filename == NULL)
    {
        return fnv1a_update(hash, "none", 4);
    }

    fp = fopen(filename, "rb");
    if (fp == NULL)
    {
        return fnv1a_update(hash, filename, strlen(filename));
    }

    got = fread(header, 1, sizeof(header), fp);
    fclose(fp);

    if (got == sizeof(header))
    {
        return fnv1a_update(hash, header, sizeof(header));
    }

    return fnv1a_update(hash, filename, strlen(filename));
}

void I_SetOpenFPGASaveIdentity(const char *iwad, const char *pwad)
{
    uint32_t hash = 0x811c9dc5u;

    hash = hash_file_identity(hash, iwad);
    hash = hash_file_identity(hash, pwad);
    hash |= 0x80000000u;

    if (hash == 0x80000000u)
    {
        hash = 0x80000001u;
    }

    current_game_id = hash;
}

uint32_t I_OpenFPGASaveGameID(void)
{
    if (current_game_id == 0)
    {
        current_game_id = 0x80000001u;
    }

    return current_game_id;
}

static int read_save_wrapper(FILE *fp, uint32_t *payload_size)
{
    uint8_t header[SAVE_WRAP_HEADER_SIZE];
    uint32_t version;
    uint32_t game_id;

    if (fread(header, 1, sizeof(header), fp) != sizeof(header))
    {
        return 0;
    }

    if (header[0] != SAVE_WRAP_MAGIC_0
     || header[1] != SAVE_WRAP_MAGIC_1
     || header[2] != SAVE_WRAP_MAGIC_2
     || header[3] != SAVE_WRAP_MAGIC_3)
    {
        return 0;
    }

    version = read_le32(header + 4);
    game_id = read_le32(header + 8);
    *payload_size = read_le32(header + 12);

    return version == SAVE_WRAP_VERSION
        && game_id == I_OpenFPGASaveGameID()
        && *payload_size > 0;
}

static int save_payload_valid(const uint8_t *data, size_t size)
{
    int has_description = 0;

    if (size < SAVESTRINGSIZE + 8)
    {
        return 0;
    }

    for (int i = 0; i < SAVESTRINGSIZE; ++i)
    {
        uint8_t c = data[i];

        if (c == '\0')
        {
            break;
        }

        if (c == 0xff || !isprint((unsigned char) c))
        {
            return 0;
        }

        has_description = 1;
    }

    return has_description
        && memcmp(data + SAVESTRINGSIZE, "version ", 8) == 0;
}

boolean I_OpenFPGASaveRead(const char *name, byte *buffer,
                           size_t capacity, size_t *length)
{
    FILE *fp;
    uint32_t payload_size;

    fp = fopen(name, "rb");
    if (fp == NULL)
    {
        return false;
    }

    if (!read_save_wrapper(fp, &payload_size)
     || payload_size > capacity
     || fread(buffer, 1, payload_size, fp) != payload_size)
    {
        fclose(fp);
        return false;
    }

    fclose(fp);

    if (!save_payload_valid(buffer, payload_size))
    {
        return false;
    }

    if (length != NULL)
    {
        *length = payload_size;
    }

    return true;
}

boolean I_OpenFPGASaveReadHeader(const char *name, byte *buffer,
                                 size_t length)
{
    FILE *fp;
    uint32_t payload_size;

    fp = fopen(name, "rb");
    if (fp == NULL)
    {
        return false;
    }

    if (!read_save_wrapper(fp, &payload_size)
     || payload_size < length
     || fread(buffer, 1, length, fp) != length)
    {
        fclose(fp);
        return false;
    }

    fclose(fp);
    return save_payload_valid(buffer, length);
}

boolean I_OpenFPGASaveWrite(const char *name, const byte *buffer,
                            size_t length)
{
    uint8_t header[SAVE_WRAP_HEADER_SIZE];
    FILE *fp;
    boolean ok;

    if (length == 0 || !save_payload_valid(buffer, length))
    {
        return false;
    }

    header[0] = SAVE_WRAP_MAGIC_0;
    header[1] = SAVE_WRAP_MAGIC_1;
    header[2] = SAVE_WRAP_MAGIC_2;
    header[3] = SAVE_WRAP_MAGIC_3;
    write_le32(header + 4, SAVE_WRAP_VERSION);
    write_le32(header + 8, I_OpenFPGASaveGameID());
    write_le32(header + 12, (uint32_t) length);

    fp = fopen(name, "wb");
    if (fp == NULL)
    {
        return false;
    }

    ok = fwrite(header, 1, sizeof(header), fp) == sizeof(header)
      && fwrite(buffer, 1, length, fp) == length;

    if (fclose(fp) != 0)
    {
        ok = false;
    }

    return ok;
}

static int new_save_exists(int slot)
{
    uint8_t header[SAVESTRINGSIZE + 8];

    return I_OpenFPGASaveReadHeader(P_SaveGameFile(slot),
                                    header, sizeof(header));
}

static int magic_at(const uint8_t *p)
{
    return p[0] == 'P' && p[1] == 'D' && p[2] == '2'
        && (p[3] == 'C' || p[3] == 'R');
}

static int decompress_lzss(const uint8_t *src, size_t src_size,
                           uint8_t *dst, size_t dst_size)
{
    size_t si = 0;
    size_t di = 0;

    while (si < src_size)
    {
        uint8_t flags = src[si++];

        for (int bit = 0; bit < 8; ++bit)
        {
            if (flags & (1 << bit))
            {
                int b0;
                int b1;
                int offset;
                int length;

                if (si + 2 > src_size)
                {
                    return (int) di;
                }

                b0 = src[si++];
                b1 = src[si++];
                offset = (b0 | ((b1 >> 4) << 8)) + 1;
                length = (b1 & 0x0f) + LZ_MIN_MATCH;

                if (offset <= 0 || di < (size_t) offset
                 || di + (size_t) length > dst_size)
                {
                    return -1;
                }

                for (int i = 0; i < length; ++i)
                {
                    dst[di] = dst[di - (size_t) offset];
                    ++di;
                }
            }
            else
            {
                if (si >= src_size)
                {
                    return (int) di;
                }

                if (di >= dst_size)
                {
                    return -1;
                }

                dst[di++] = src[si++];
            }
        }
    }

    return (int) di;
}

static int decode_v2_slot(const uint8_t *base, size_t max_size,
                          uint8_t *out, size_t *out_size)
{
    uint32_t stored_size;
    uint32_t raw_size;

    if (max_size < LEGACY_V2_HEADER_SIZE)
    {
        return 0;
    }

    stored_size = read_le32(base + 8);
    raw_size = read_le32(base + 12);

    if (stored_size == 0 || raw_size == 0
     || stored_size > max_size - LEGACY_V2_HEADER_SIZE
     || raw_size > LEGACY_DECODE_SIZE)
    {
        return 0;
    }

    if (base[3] == 'C')
    {
        int result = decompress_lzss(base + LEGACY_V2_HEADER_SIZE,
                                     stored_size, out, raw_size);
        if (result < 0 || (uint32_t) result != raw_size)
        {
            return 0;
        }
    }
    else
    {
        if (stored_size != raw_size)
        {
            return 0;
        }

        memcpy(out, base + LEGACY_V2_HEADER_SIZE, raw_size);
    }

    if (!save_payload_valid(out, raw_size))
    {
        return 0;
    }

    *out_size = raw_size;
    return 1;
}

static int decode_v1_slot(const uint8_t *slot, uint8_t *out, size_t *out_size)
{
    uint32_t game_id = read_le32(slot);
    uint32_t raw_size = read_le32(slot + 4);

    if (game_id == 0 || game_id > 5
     || raw_size == 0
     || raw_size > LEGACY_SLOT_SIZE - LEGACY_V1_HEADER_SIZE
     || slot[LEGACY_V1_HEADER_SIZE] != ' ')
    {
        return 0;
    }

    memcpy(out, slot + LEGACY_V1_HEADER_SIZE, raw_size);

    if (!save_payload_valid(out, raw_size))
    {
        return 0;
    }

    *out_size = raw_size;
    return 1;
}

static int decode_legacy_slot(const uint8_t *slot, uint8_t *out,
                              size_t *out_size)
{
    if (magic_at(slot + LEGACY_PAD_SIZE))
    {
        return decode_v2_slot(slot + LEGACY_PAD_SIZE,
                              LEGACY_SLOT_SIZE - LEGACY_PAD_SIZE,
                              out, out_size);
    }

    if (magic_at(slot))
    {
        return decode_v2_slot(slot, LEGACY_SLOT_SIZE, out, out_size);
    }

    return decode_v1_slot(slot, out, out_size);
}

void I_MigratePocketDoomSaves(void)
{
    uint8_t *legacy;
    uint8_t *decoded;
    FILE *fp;
    size_t bytes_read;
    int imported = 0;

    fp = fopen(LEGACY_FILENAME, "rb");
    if (fp == NULL)
    {
        return;
    }

    legacy = malloc(LEGACY_FILE_SIZE);
    decoded = malloc(LEGACY_DECODE_SIZE);

    if (legacy == NULL || decoded == NULL)
    {
        fclose(fp);
        free(legacy);
        free(decoded);
        return;
    }

    memset(legacy, 0, LEGACY_FILE_SIZE);
    bytes_read = fread(legacy, 1, LEGACY_FILE_SIZE, fp);
    fclose(fp);

    if (bytes_read == 0)
    {
        free(legacy);
        free(decoded);
        return;
    }

    for (int slot = 0; slot < LEGACY_SLOT_COUNT; ++slot)
    {
        size_t decoded_size = 0;
        const uint8_t *legacy_slot = legacy + (size_t) slot * LEGACY_SLOT_SIZE;

        if ((size_t) slot * LEGACY_SLOT_SIZE >= bytes_read)
        {
            break;
        }

        if (new_save_exists(slot))
        {
            continue;
        }

        memset(decoded, 0, LEGACY_DECODE_SIZE);
        if (!decode_legacy_slot(legacy_slot, decoded, &decoded_size))
        {
            continue;
        }

        if (I_OpenFPGASaveWrite(P_SaveGameFile(slot),
                                decoded, decoded_size))
        {
            ++imported;
            printf("PocketDoom save migration: imported slot %d (%u bytes)\n",
                   slot, (unsigned) decoded_size);
        }
    }

    if (imported > 0)
    {
        printf("PocketDoom save migration: imported %d save slot%s\n",
               imported, imported == 1 ? "" : "s");
    }

    free(legacy);
    free(decoded);
}

#else

#include "i_save.h"

void I_SetOpenFPGASaveIdentity(const char *iwad, const char *pwad)
{
    (void) iwad;
    (void) pwad;
}

uint32_t I_OpenFPGASaveGameID(void)
{
    return 0;
}

boolean I_OpenFPGASaveRead(const char *name, byte *buffer,
                           size_t capacity, size_t *length)
{
    (void) name;
    (void) buffer;
    (void) capacity;
    (void) length;
    return false;
}

boolean I_OpenFPGASaveReadHeader(const char *name, byte *buffer,
                                 size_t length)
{
    (void) name;
    (void) buffer;
    (void) length;
    return false;
}

boolean I_OpenFPGASaveWrite(const char *name, const byte *buffer,
                            size_t length)
{
    (void) name;
    (void) buffer;
    (void) length;
    return false;
}

void I_MigratePocketDoomSaves(void)
{
}

#endif
