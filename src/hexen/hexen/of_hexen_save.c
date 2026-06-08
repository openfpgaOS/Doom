/* of_hexen_save.c — Pocket save layer for Hexen (not upstream chocolate-doom).
 *
 * Hexen's hub saves are many files (hex6.hxs + a hex6NN.hxs per visited map),
 * ~520-556 KB a hub, over the Pocket's 256 KB NVRAM slot. During play the files
 * live in an in-RAM VFS (sv_save.c rerouted here, #ifndef OF_PC); a manual save
 * packs a slot's files into one LZSS-compressed .sav (~132 KB worst case). The
 * LZSS codec is shared with a desktop round-trip test so it compiles in every
 * build; the VFS/NVRAM layer is Pocket-only. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "p_saveg.h"

/* ── LZSS (always compiled; bitstream matches the shim's decompressor) ──────
 * Control byte holds 8 token flags, LSB first: 0 = literal byte, 1 = a 2-byte
 * back-reference. offset is 12 bits stored as offset-1 (1..4096), length is 4
 * bits stored as length-3 (3..18). */
#define OFX_LZ_WINDOW   4096
#define OFX_LZ_MINMATCH 3
#define OFX_LZ_MAXMATCH 18
#define OFX_LZ_HBITS    13
#define OFX_LZ_HSIZE    (1 << OFX_LZ_HBITS)
#define OFX_LZ_MAXCHAIN 64

static unsigned ofx_hash3(const unsigned char *p)
{
    unsigned h = ((unsigned) p[0] << 16) ^ ((unsigned) p[1] << 8) ^ p[2];
    return (h * 2654435761u) >> (32 - OFX_LZ_HBITS);
}

int OFX_LzssCompress(const unsigned char *src, int srclen,
                     unsigned char *dst, int dstcap)
{
    int *head;
    int *prev;
    int di = 0;
    int i = 0;
    int ctrl_pos = 0;
    int ctrl_bit = 0;
    unsigned char ctrl = 0;

    head = malloc(OFX_LZ_HSIZE * sizeof(int));
    prev = malloc((srclen > 0 ? srclen : 1) * sizeof(int));
    if (head == NULL || prev == NULL)
    {
        free(head);
        free(prev);
        return -1;
    }
    for (i = 0; i < OFX_LZ_HSIZE; ++i)
    {
        head[i] = -1;
    }

    i = 0;
    while (i < srclen)
    {
        int best_len = 0;
        int best_off = 0;

        if (ctrl_bit == 0)
        {
            if (di >= dstcap) goto overflow;
            ctrl_pos = di;
            dst[di++] = 0;
            ctrl = 0;
        }

        if (i + OFX_LZ_MINMATCH <= srclen)
        {
            unsigned hh = ofx_hash3(src + i);
            int cand = head[hh];
            int chain = 0;
            int maxlen = srclen - i;

            if (maxlen > OFX_LZ_MAXMATCH) maxlen = OFX_LZ_MAXMATCH;
            while (cand >= 0 && (i - cand) <= OFX_LZ_WINDOW
                && chain < OFX_LZ_MAXCHAIN)
            {
                int l = 0;
                while (l < maxlen && src[cand + l] == src[i + l]) ++l;
                if (l > best_len)
                {
                    best_len = l;
                    best_off = i - cand;
                    if (l >= maxlen) break;
                }
                cand = prev[cand];
                ++chain;
            }
        }

        if (best_len >= OFX_LZ_MINMATCH)
        {
            int off = best_off - 1;
            int len = best_len - OFX_LZ_MINMATCH;
            int end = i + best_len;

            ctrl |= (unsigned char) (1 << ctrl_bit);
            dst[ctrl_pos] = ctrl;
            if (di + 2 > dstcap) goto overflow;
            dst[di++] = (unsigned char) (off & 0xff);
            dst[di++] = (unsigned char) (((off >> 8) << 4) | (len & 0x0f));

            while (i < end)
            {
                if (i + OFX_LZ_MINMATCH <= srclen)
                {
                    unsigned hh = ofx_hash3(src + i);
                    prev[i] = head[hh];
                    head[hh] = i;
                }
                ++i;
            }
        }
        else
        {
            if (di >= dstcap) goto overflow;
            dst[di++] = src[i];
            if (i + OFX_LZ_MINMATCH <= srclen)
            {
                unsigned hh = ofx_hash3(src + i);
                prev[i] = head[hh];
                head[hh] = i;
            }
            ++i;
        }

        ctrl_bit = (ctrl_bit + 1) & 7;
    }

    free(head);
    free(prev);
    return di;

overflow:
    free(head);
    free(prev);
    return -1;
}

int OFX_LzssDecompress(const unsigned char *src, int srclen,
                       unsigned char *dst, int dstcap)
{
    int si = 0;
    int di = 0;

    while (si < srclen)
    {
        unsigned char flags = src[si++];
        int bit;

        for (bit = 0; bit < 8; ++bit)
        {
            if (flags & (1 << bit))
            {
                int b0;
                int b1;
                int off;
                int len;
                int k;

                if (si + 2 > srclen) return di;
                b0 = src[si++];
                b1 = src[si++];
                off = (b0 | ((b1 >> 4) << 8)) + 1;
                len = (b1 & 0x0f) + OFX_LZ_MINMATCH;
                if (di < off || di + len > dstcap) return -1;
                for (k = 0; k < len; ++k)
                {
                    dst[di] = dst[di - off];
                    ++di;
                }
            }
            else
            {
                if (si >= srclen) return di;
                if (di >= dstcap) return -1;
                dst[di++] = src[si++];
            }
        }
    }
    return di;
}

/* ── In-RAM VFS + NVRAM slot persistence (Pocket only) ─────────────────── */
#ifndef OF_PC

#include <stdio.h>

#include "doomtype.h"
#include "i_save.h"
#include "m_misc.h"

#define OFX_MAX_ENTRIES 64
#define OFX_NAME_MAX    24
#define OFX_WCAP        (512 * 1024)   /* scratch for one .hxs file write   */
#define OFX_SLOT_RAW    (768 * 1024)   /* packed/compressed whole-hub blob  */
#define OFX_SLOT_LIMIT  0x40000        /* per data.json: 256 KB per .sav    */
#define OFX_WRAP_HDR    40             /* 4 magic +4 id +4 raw +4 comp +24 desc */

typedef struct
{
    char name[OFX_NAME_MAX];
    unsigned char *data;
    int len;
} ofx_entry_t;

static ofx_entry_t ofx_vfs[OFX_MAX_ENTRIES];
static int ofx_count = 0;

static unsigned char ofx_wbuf[OFX_WCAP];
static FILE *ofx_wfp = NULL;
static char ofx_wname[OFX_NAME_MAX];

static unsigned char ofx_raw[OFX_SLOT_RAW];
static unsigned char ofx_comp[OFX_SLOT_RAW];

static char ofx_prefix[32] = "hexen";
static char ofx_slotfile[48];

static const char *ofx_base(const char *path)
{
    const char *b = path;
    const char *p;

    for (p = path; *p; ++p)
    {
        if (*p == '/' || *p == '\\') b = p + 1;
    }
    return b;
}

static ofx_entry_t *ofx_find(const char *name)
{
    const char *b = ofx_base(name);
    int i;

    for (i = 0; i < ofx_count; ++i)
    {
        if (!strcmp(ofx_vfs[i].name, b)) return &ofx_vfs[i];
    }
    return NULL;
}

static void ofx_set(const char *name, const unsigned char *data, int len)
{
    ofx_entry_t *e = ofx_find(name);

    if (len < 0) len = 0;
    if (e == NULL)
    {
        if (ofx_count >= OFX_MAX_ENTRIES) return;
        e = &ofx_vfs[ofx_count++];
        memset(e, 0, sizeof(*e));
        M_StringCopy(e->name, ofx_base(name), sizeof(e->name));
    }
    else
    {
        free(e->data);
        e->data = NULL;
        e->len = 0;
    }

    e->data = malloc(len > 0 ? len : 1);
    if (e->data != NULL)
    {
        if (len > 0) memcpy(e->data, data, len);
        e->len = len;
    }
}

/* matches "hex<slot>.hxs" and "hex<slot>NN.hxs" */
static boolean ofx_in_slot(const char *n, int slot)
{
    return n[0] == 'h' && n[1] == 'e' && n[2] == 'x'
        && n[3] == (char) ('0' + slot)
        && (n[4] == '.' || (n[4] >= '0' && n[4] <= '9'));
}

void *OFX_OpenWrite(const char *name)
{
    M_StringCopy(ofx_wname, ofx_base(name), sizeof(ofx_wname));
    ofx_wfp = fmemopen(ofx_wbuf, OFX_WCAP, "wb");
    return ofx_wfp;
}

void *OFX_OpenRead(const char *name)
{
    ofx_entry_t *e = ofx_find(name);

    if (e == NULL) return NULL;
    return fmemopen(e->data, e->len, "rb");
}

void OFX_BeforeClose(void *fp)
{
    if (fp != NULL && fp == ofx_wfp)
    {
        long n;

        fflush((FILE *) fp);
        n = ftell((FILE *) fp);
        if (n < 0) n = 0;
        ofx_set(ofx_wname, ofx_wbuf, (int) n);
        ofx_wfp = NULL;
    }
}

boolean OFX_Exists(const char *name)
{
    return ofx_find(name) != NULL;
}

void OFX_Remove(const char *name)
{
    const char *b = ofx_base(name);
    int i;

    for (i = 0; i < ofx_count; ++i)
    {
        if (!strcmp(ofx_vfs[i].name, b))
        {
            free(ofx_vfs[i].data);
            ofx_vfs[i] = ofx_vfs[--ofx_count];
            return;
        }
    }
}

void OFX_Copy(const char *src, const char *dst)
{
    ofx_entry_t *s = ofx_find(src);

    if (s == NULL) return;
    ofx_set(dst, s->data, s->len);
}

static void ofx_put_le32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char) v;
    p[1] = (unsigned char) (v >> 8);
    p[2] = (unsigned char) (v >> 16);
    p[3] = (unsigned char) (v >> 24);
}

static uint32_t ofx_get_le32(const unsigned char *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8)
         | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

/* Concatenate a slot's VFS files into buf: [u32 count] then per file
 * [u8 namelen][name][u32 len][data]. Returns packed length or -1. */
static int ofx_pack_slot(int slot, unsigned char *buf, int cap)
{
    int di = 0;
    int cnt = 0;
    int i;

    for (i = 0; i < ofx_count; ++i)
    {
        if (ofx_in_slot(ofx_vfs[i].name, slot)) ++cnt;
    }
    if (di + 4 > cap) return -1;
    ofx_put_le32(buf + di, (uint32_t) cnt);
    di += 4;

    for (i = 0; i < ofx_count; ++i)
    {
        ofx_entry_t *e = &ofx_vfs[i];
        int nl;

        if (!ofx_in_slot(e->name, slot)) continue;
        nl = (int) strlen(e->name);
        if (di + 1 + nl + 4 + e->len > cap) return -1;
        buf[di++] = (unsigned char) nl;
        memcpy(buf + di, e->name, nl);
        di += nl;
        ofx_put_le32(buf + di, (uint32_t) e->len);
        di += 4;
        memcpy(buf + di, e->data, e->len);
        di += e->len;
    }
    return di;
}

void OFX_FlushSlot(int slot)
{
    int rawlen;
    int clen;
    FILE *fp;
    unsigned char hdr[OFX_WRAP_HDR];
    ofx_entry_t *g;
    char gname[16];

    rawlen = ofx_pack_slot(slot, ofx_raw, sizeof(ofx_raw));
    if (rawlen < 0) return;
    clen = OFX_LzssCompress(ofx_raw, rawlen, ofx_comp, sizeof(ofx_comp));
    if (clen < 0) return;

    if (OFX_WRAP_HDR + clen > OFX_SLOT_LIMIT)
    {
        /* Save would overflow the NVRAM slot; refuse rather than corrupt. */
        fprintf(stderr, "OFX_FlushSlot: slot %d too large (%d > %d), not saved\n",
                slot, OFX_WRAP_HDR + clen, OFX_SLOT_LIMIT);
        return;
    }

    fp = fopen(P_SaveGameFile(slot), "wb");
    if (fp == NULL) return;

    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'H'; hdr[1] = 'X'; hdr[2] = 'S'; hdr[3] = 'V';
    ofx_put_le32(hdr + 4, I_OpenFPGASaveGameID());
    ofx_put_le32(hdr + 8, (uint32_t) rawlen);
    ofx_put_le32(hdr + 12, (uint32_t) clen);

    M_snprintf(gname, sizeof(gname), "hex%d.hxs", slot);
    g = ofx_find(gname);
    if (g != NULL && g->len >= SAVESTRINGSIZE)
    {
        memcpy(hdr + 16, g->data, SAVESTRINGSIZE);
    }

    fwrite(hdr, 1, sizeof(hdr), fp);
    fwrite(ofx_comp, 1, clen, fp);
    fclose(fp);
}

void OFX_EnsureSlot(int slot)
{
    FILE *fp;
    unsigned char hdr[OFX_WRAP_HDR];
    uint32_t rawlen;
    uint32_t clen;
    int got;
    int di;
    int cnt;
    int e;

    fp = fopen(P_SaveGameFile(slot), "rb");
    if (fp == NULL) return;               /* no NVRAM save (e.g. reborn slot) */

    if (fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)
     || hdr[0] != 'H' || hdr[1] != 'X' || hdr[2] != 'S' || hdr[3] != 'V'
     || ofx_get_le32(hdr + 4) != I_OpenFPGASaveGameID())
    {
        fclose(fp);
        return;
    }

    rawlen = ofx_get_le32(hdr + 8);
    clen = ofx_get_le32(hdr + 12);
    if (clen > sizeof(ofx_comp) || rawlen > sizeof(ofx_raw)
     || fread(ofx_comp, 1, clen, fp) != clen)
    {
        fclose(fp);
        return;
    }
    fclose(fp);

    got = OFX_LzssDecompress(ofx_comp, (int) clen, ofx_raw, sizeof(ofx_raw));
    if (got != (int) rawlen) return;

    cnt = (int) ofx_get_le32(ofx_raw);
    di = 4;
    for (e = 0; e < cnt && di < got; ++e)
    {
        int snl;
        int cnl;
        int dl;
        char nm[OFX_NAME_MAX];

        snl = ofx_raw[di++];
        if (di + snl + 4 > got) break;
        cnl = snl < OFX_NAME_MAX - 1 ? snl : OFX_NAME_MAX - 1;
        memcpy(nm, ofx_raw + di, cnl);
        nm[cnl] = '\0';
        di += snl;
        dl = (int) ofx_get_le32(ofx_raw + di);
        di += 4;
        if (dl < 0 || di + dl > got) break;
        ofx_set(nm, ofx_raw + di, dl);
        di += dl;
    }
}

boolean OFX_SlotDescription(int slot, char *desc)
{
    FILE *fp;
    unsigned char hdr[OFX_WRAP_HDR];
    boolean ok;

    fp = fopen(P_SaveGameFile(slot), "rb");
    if (fp == NULL) return false;

    ok = fread(hdr, 1, sizeof(hdr), fp) == sizeof(hdr)
      && hdr[0] == 'H' && hdr[1] == 'X' && hdr[2] == 'S' && hdr[3] == 'V'
      && ofx_get_le32(hdr + 4) == I_OpenFPGASaveGameID();
    if (ok)
    {
        memcpy(desc, hdr + 16, SAVESTRINGSIZE);
    }
    fclose(fp);
    return ok;
}

void P_SetOpenFPGASavePrefix(const char *prefix)
{
    char *dot;

    if (prefix == NULL || prefix[0] == '\0') return;
    M_StringCopy(ofx_prefix, ofx_base(prefix), sizeof(ofx_prefix));
    dot = strchr(ofx_prefix, '.');
    if (dot != NULL) *dot = '\0';
}

char *P_SaveGameFile(int slot)
{
    M_snprintf(ofx_slotfile, sizeof(ofx_slotfile), "%s_%d.sav", ofx_prefix, slot);
    return ofx_slotfile;
}

#endif /* OF_PC */
