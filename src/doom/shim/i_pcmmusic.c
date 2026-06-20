/* i_pcmmusic.c — stream PCM music from a WAD lump over the HW mixer, else MIDI (not upstream).
 *
 * Doom core only.  S_ChangeMusic's lump D_E1M1 maps to a 'P'-prefixed lump
 * (PE1M1) of raw 48 kHz / stereo / s16le PCM in a merged music WAD; present ->
 * stream it, absent -> the OPL/MIDI module keeps playing.
 *
 * Playback model (mirrors Quake's cd_of.c, which never underruns): two mono HW
 * mixer voices (L hard-left, R hard-right, MUSIC group) loop a deinterleaved
 * SDRAM ring forever — the FPGA mixer DMAs+loops it with no CPU audio deadline,
 * unlike of_audio_stream (a CPU-fed FIFO that goes silent the moment Poll is
 * late, e.g. a level load).  Each Poll reads the play cursor and tops the ring
 * up *behind* it; of_cache_flush_range after every write (the mixer fetch is an
 * external AXI master).
 *
 * Refill is async DMA: of_file_read_async hands the SD transfer to the
 * data-slot engine (a blocking fread of the 192 KB/s stream costs ~13% CPU),
 * the bytes land in CRAM0 in the background, and a completion IRQ flags them;
 * the next Poll deinterleaves the finished chunk into the ring.  The single
 * data-slot bridge serves one transfer at a time, so any engine blocking read
 * (level load, save) must first retire an in-flight CD read — I_PCM_DrainAsync()
 * is called from W_StdC_Read and the save path.  Falls back to synchronous
 * W_Read when async is unavailable; prefill is always synchronous.
 */

#include "config.h"

#ifdef OF_DOOM

#include "doomtype.h"
#include "doomdef.h"      /* gameaction_t / ga_savegame / ga_loadgame */
#include "i_sound.h"
#include "w_wad.h"
#include "w_file.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "of_mixer.h"
#include "of_cache.h"
#include "of_file.h"
#include "of_timer.h"
#include "of_video.h"

#define PCM_WAD         "DOOMMUS.WAD"
#define MUS_RATE        48000          /* source + voice rate (no resample) */
#define MUS_CHANNELS    2
#define MUS_BYTES_FRAME (MUS_CHANNELS * (int)sizeof(int16_t))   /* 4 */
#define RING_SECONDS    4              /* refill slack; covers a level load */
#define RING_FRAMES     (MUS_RATE * RING_SECONDS)
#define STAGING_FRAMES  4096           /* one sync read+deinterleave pass */
#define CHUNK_FRAMES    16384          /* async DMA read (64 KB), max */
#define MUS_PRIORITY    200
#define PRESENT_STALE_US 250000u       /* no scanout for this long == OSD/stall */

/* Output boost: the mixer master is already at its 255 max, so an extra +50%
 * is a software gain on the samples, hard-clamped so peaks limit (not wrap). */
#define MASTER_GAIN_NUM 15
#define MASTER_GAIN_DEN 10

static inline int16_t gain_clamp(int s)
{
    s = s * MASTER_GAIN_NUM / MASTER_GAIN_DEN;
    if (s > 32767)  s = 32767;
    else if (s < -32768) s = -32768;
    return (int16_t)s;
}

void I_OpenFPGAMixerPump(void);        /* shared SFX mixer maintenance (i_sdlsound.c) */

/* Exported (read by the bridge-drain hooks in w_file_stdc.c / i_save_migrate.c):
 * nonzero only while CD music is the active source, so those hooks stay inert
 * for MIDI / no-WAD play. */
int                i_pcm_active;

static char        pcm_lump[9];        /* "PE1M1" etc. — empty = no track */
static int         pcm_checked;        /* WAD merge + async setup attempted */
static int         pcm_ready;          /* WAD present (rings usable) */
static int         pcm_playing;
static int         pcm_looping;
static int         pcm_paused;
static int         pcm_volume = 127;   /* 0..127 */
static int         pcm_ended;          /* non-looping track read to EOF */

/* source: a byte range within the merged WAD file */
static wad_file_t *pcm_wad;
static unsigned    pcm_base;           /* lump file offset */
static unsigned    pcm_size;           /* lump byte length (whole frames) */
static unsigned    pcm_rd;             /* sync read cursor within the lump */

/* deinterleaved SDRAM rings (+1 guard frame mirrors [0] for the loop seam) */
static int16_t     ringL[RING_FRAMES + 1];
static int16_t     ringR[RING_FRAMES + 1];
static int16_t     staging[STAGING_FRAMES * MUS_CHANNELS];
static int         write_pos;          /* next ring frame to fill */
static int         last_pos;           /* play cursor at previous refill */
static int         last_vol;           /* last group volume pushed (0..255) */
static int         ring_valid;         /* real (non-silence) frames ahead of cursor */

static of_mixer_handle_t vL = OF_MIXER_HANDLE_INVALID;
static of_mixer_handle_t vR = OF_MIXER_HANDLE_INVALID;

/* ---- async (data-slot DMA) refill state --------------------------- */
static int           cd_slot = -1;     /* data slot of the music WAD */
static uint8_t      *cd_stage;          /* CRAM0 staging for of_file_read_async */
static int           cd_chunk;          /* frames per DMA read (<= CHUNK_FRAMES) */
static int           cd_async_ok;       /* async usable for the current track */
static int           cd_pending;        /* a DMA read is in flight */
static int           cd_frames;         /* frames the in-flight read delivers */
static unsigned      cd_read_off;       /* async read cursor within the lump */
static volatile int  cd_done;           /* set by the completion IRQ callback */
static volatile int  cd_result;         /* IRQ result: 0 ok, <0 error */
static int           cd_menu_prev;      /* `quiet` (menu/save/load) last Poll, edge detect */

/* Async DMA refill is ENABLED, but suspended while frame presentation is
 * stalled.  When the Analogue Pocket *system* menu opens, the Pocket owns the
 * display (scanout stops) and the shared single data-slot bridge; an in-flight
 * of_file_read_async issued then gets stuck on the bridge and the stalled
 * transfer starves the GPU until its watchdog traps (==TRAP== mcause=3 in
 * of_gpu_wait — reproduced only with CD music).  We detect the stall via the
 * OS present_count freezing (of_video_get_timing) and, like Quake, freeze the
 * voices (the ring repeats the last sample) and issue no DMA until scanout
 * resumes. */
static const int     pcm_async_enabled = 1;
static int           pcm_frozen;            /* voices frozen during a scanout stall */
static uint32_t      pcm_present_count;      /* last of_video present_count seen */
static uint32_t      pcm_present_change_us;  /* of_time_us when present_count last moved */

void I_SetMusicTrackName(const char *name)
{
    size_t i = 0;

    pcm_lump[0] = '\0';
    if (name == NULL || name[0] == '\0')
        return;
    if ((name[0] == 'd' || name[0] == 'D') && name[1] == '_')
        name += 2;

    pcm_lump[i++] = 'P';
    for (; i < 8 && *name; name++)
    {
        char c = *name;
        pcm_lump[i++] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    pcm_lump[i] = '\0';
}

int I_PCM_Active(void)
{
    return pcm_playing;
}

static int pcm_group_volume(void)
{
    return (pcm_volume * 255) / 127;     /* 0..127 -> 0..255 */
}

/* Write `frames` interleaved frames from src into the ring at the write cursor,
 * wrapping, flushing each span and keeping the loop-seam guard in sync. */
static void ring_write(const int16_t *src, int frames)
{
    int done = 0;

    while (done < frames)
    {
        int w = write_pos;
        int contig = RING_FRAMES - w;
        int batch = frames - done;
        int i;

        if (batch > contig)
            batch = contig;

        for (i = 0; i < batch; i++)
        {
            ringL[w + i] = gain_clamp(src[(done + i) * 2 + 0]);
            ringR[w + i] = gain_clamp(src[(done + i) * 2 + 1]);
        }
        of_cache_flush_range(&ringL[w], (uint32_t)batch * sizeof(int16_t));
        of_cache_flush_range(&ringR[w], (uint32_t)batch * sizeof(int16_t));

        if (w == 0)
        {
            ringL[RING_FRAMES] = ringL[0];
            ringR[RING_FRAMES] = ringR[0];
            of_cache_flush_range(&ringL[RING_FRAMES], sizeof(int16_t));
            of_cache_flush_range(&ringR[RING_FRAMES], sizeof(int16_t));
        }

        write_pos = (w + batch) % RING_FRAMES;
        done += batch;
    }
}

/* Synchronous read of `count` interleaved frames into staging, looping within
 * the lump.  Returns the count of *real* (non-EOF-pad) frames. */
static int pcm_read_frames(int count)
{
    int got = 0, real = 0;

    while (got < count)
    {
        unsigned remain = pcm_size - pcm_rd;
        unsigned want;
        size_t   n;

        if (remain == 0)
        {
            if (pcm_looping) { pcm_rd = 0; remain = pcm_size; }
            else
            {
                pcm_ended = 1;
                memset(&staging[got * MUS_CHANNELS], 0,
                       (size_t)(count - got) * MUS_BYTES_FRAME);
                return real;
            }
        }

        want = (unsigned)(count - got) * MUS_BYTES_FRAME;
        if (want > remain) want = remain;

        n = W_Read(pcm_wad, pcm_base + pcm_rd, &staging[got * MUS_CHANNELS], want);
        if (n == 0)
        {
            memset(&staging[got * MUS_CHANNELS], 0,
                   (size_t)(count - got) * MUS_BYTES_FRAME);
            return real;
        }
        pcm_rd += (unsigned)n;
        got    += (int)n / MUS_BYTES_FRAME;
        real   += (int)n / MUS_BYTES_FRAME;
    }
    return real;
}

/* Synchronous producer: read+deinterleave `nframes` into the ring. */
static void pcm_produce(int nframes)
{
    while (nframes > 0)
    {
        int batch = nframes;
        int real;
        if (batch > STAGING_FRAMES)
            batch = STAGING_FRAMES;
        real = pcm_read_frames(batch);
        ring_write(staging, batch);
        ring_valid += real;
        nframes -= batch;
    }
}

/* ---- async refill ------------------------------------------------- */
#ifndef OF_PC
static void cd_cb(int token, int result)
{
    (void)token;
    cd_result = result;
    cd_done = 1;
}

/* Kick a DMA read of up to cd_chunk frames at cd_read_off, looping/EOF.
 * Non-blocking: sets cd_pending and pre-advances the read cursor. */
static int cd_issue(void)
{
    unsigned remain = pcm_size - cd_read_off;
    int frames, tok;

    if (remain == 0)
    {
        if (!pcm_looping) { pcm_ended = 1; return -1; }
        cd_read_off = 0;
        remain = pcm_size;
    }
    frames = cd_chunk;
    if ((unsigned)frames * MUS_BYTES_FRAME > remain)
        frames = (int)(remain / MUS_BYTES_FRAME);
    if (frames <= 0)
        return -1;

    cd_done = 0;
    cd_result = -1;
    tok = of_file_read_async(cd_slot, pcm_base + cd_read_off, cd_stage,
                             (uint32_t)(frames * MUS_BYTES_FRAME), cd_cb);
    if (tok < 0) { cd_async_ok = 0; return tok; }

    cd_pending = 1;
    cd_frames  = frames;
    cd_read_off += (unsigned)frames * MUS_BYTES_FRAME;
    return 0;
}
#else
static int cd_issue(void) { cd_async_ok = 0; return -1; }
#endif

/* Retire an in-flight DMA read so the single-slot bridge is free for the
 * engine's own blocking file I/O.  Called from W_StdC_Read and the save path.
 * Bounded wait; on timeout, drop to the sync refill path. */
void I_PCM_DrainAsync(void)
{
#ifndef OF_PC
    unsigned start;

    if (!cd_pending || cd_done)
        return;

    start = of_time_ms();
    while (!cd_done)
    {
        of_file_async_poll();
        if (cd_done)
            break;
        if (!of_file_async_busy())
            break;
        if ((unsigned)(of_time_ms() - start) >= 200u)
        {
            cd_async_ok = 0;        /* wedged — sync from here */
            break;
        }
    }
#endif
}

static void pcm_stop_voices(void)
{
    if (vL != OF_MIXER_HANDLE_INVALID) { of_mixer_stop_h(vL); vL = OF_MIXER_HANDLE_INVALID; }
    if (vR != OF_MIXER_HANDLE_INVALID) { of_mixer_stop_h(vR); vR = OF_MIXER_HANDLE_INVALID; }
}

void I_PCM_Stop(void)
{
    if (!pcm_playing)
        return;
    I_PCM_DrainAsync();             /* free the bridge before the next track */
    cd_pending = 0;
    cd_async_ok = 0;
    pcm_stop_voices();
    pcm_playing = 0;
    i_pcm_active = 0;
    pcm_paused = 0;
    pcm_ended = 0;
}

/* Merge the optional music WAD + set up async refill on first use (graceful if
 * the WAD or async support is absent). */
static void pcm_ensure_init(void)
{
    if (pcm_checked)
        return;
    pcm_checked = 1;

    if (W_AddFile(PCM_WAD) == NULL)
    {
        printf("CD music: %s absent, MIDI only\n", PCM_WAD);
        return;
    }
    W_GenerateHashTable();
    pcm_ready = 1;

#ifndef OF_PC
    if (pcm_async_enabled)
    {
        uint32_t slot, maxr;
        if (of_file_slot_find(PCM_WAD, &slot) == 0)
        {
            cd_slot = (int)slot;
            cd_chunk = CHUNK_FRAMES;
            maxr = of_file_async_max_read();
            if (maxr > 0 && (uint32_t)(cd_chunk * MUS_BYTES_FRAME) > maxr)
                cd_chunk = (int)(maxr / MUS_BYTES_FRAME);
            if (cd_chunk > 0)
                cd_stage = (uint8_t *)of_file_dma_stage_alloc(
                    (uint32_t)cd_chunk * MUS_BYTES_FRAME, 2048);
        }
    }
#endif
    printf("CD music: %s loaded (%s refill)\n", PCM_WAD,
           cd_stage ? "async DMA" : "sync");
}

int I_PCM_TryPlay(boolean looping)
{
    lumpindex_t n;
    lumpinfo_t *l;

    pcm_ensure_init();
    if (!pcm_ready || pcm_lump[0] == '\0')
        return 0;

    n = W_CheckNumForName(pcm_lump);
    if (n < 0)
        return 0;
    l = lumpinfo[n];
    if (l == NULL || l->size < MUS_BYTES_FRAME)
        return 0;

    if (pcm_playing)
        I_PCM_Stop();

    pcm_looping = looping ? 1 : 0;
    pcm_paused  = 0;
    pcm_ended   = 0;
    pcm_wad     = l->wad_file;
    pcm_base    = (unsigned)l->position;
    pcm_size    = (unsigned)l->size & ~3u;
    pcm_rd      = 0;

    /* Prefill the whole ring synchronously (one-time at track start). */
    write_pos  = 0;
    ring_valid = 0;
    pcm_produce(RING_FRAMES);
    write_pos  = 0;
    last_pos   = 0;

    /* Steady-state goes async if enabled and the data-slot DMA is available. */
    cd_async_ok = (pcm_async_enabled && cd_stage != NULL && cd_slot >= 0);
    cd_pending  = 0;
    cd_read_off = pcm_rd;          /* async resumes where the prefill left off */
    pcm_frozen  = 0;
    pcm_present_count = 0;
    pcm_present_change_us = of_time_us();

    vL = of_mixer_alloc_for_group_h(OF_MIXER_GROUP_MUSIC, (const uint8_t *)ringL,
                                    RING_FRAMES, MUS_RATE, MUS_PRIORITY, 0);
    vR = of_mixer_alloc_for_group_h(OF_MIXER_GROUP_MUSIC, (const uint8_t *)ringR,
                                    RING_FRAMES, MUS_RATE, MUS_PRIORITY, 0);
    if (vL == OF_MIXER_HANDLE_INVALID || vR == OF_MIXER_HANDLE_INVALID)
    {
        printf("CD music: no free music voices, using MIDI\n");
        pcm_stop_voices();
        return 0;
    }

    of_mixer_set_loop_h(vL, 0, RING_FRAMES);
    of_mixer_set_loop_h(vR, 0, RING_FRAMES);
    of_mixer_set_vol_lr_h(vL, 255, 0);
    of_mixer_set_vol_lr_h(vR, 0, 255);

    last_vol = pcm_group_volume();
    of_mixer_set_group_volume(OF_MIXER_GROUP_MUSIC, last_vol);

    pcm_playing = 1;
    i_pcm_active = 1;
    printf("CD music: streaming lump %s (%u bytes, %s)\n", pcm_lump, pcm_size,
           cd_async_ok ? "async" : "sync");
    return 1;
}

/* Fold a finished DMA read into the ring. */
static void cd_fold(void)
{
    if (cd_async_ok && cd_pending && cd_done)
    {
        if (cd_result >= 0)
        {
            ring_write((const int16_t *)cd_stage, cd_frames);
            ring_valid += cd_frames;
        }
        cd_pending = 0;
    }
}

void I_PCM_Poll(void)
{
    if (pcm_playing)
    {
        extern boolean menuactive;
        extern gameaction_t gameaction;
        extern boolean sendsave;
        /* Suspend async CD reads across the WHOLE save/load sequence, not just
         * while the menu is up: the menu closes (menuactive=0) a few tics BEFORE
         * G_DoSaveGame runs the blocking slot write, and a read issued in that
         * gap collides with the save on the single data-slot bridge (GPU
         * watchdog hang).  sendsave covers menu-confirm -> button; gameaction
         * covers the tic the blocking save/load I/O actually executes. */
        int quiet = menuactive || sendsave
                 || gameaction == ga_savegame
                 || gameaction == ga_loadgame;
        int pos, consumed, present_live;
        of_video_timing_t vt;
        uint32_t now_us = of_time_us();

        cd_fold();

        /* Track scanout: present_count advances each time the Pocket presents a
         * core frame.  It freezes while the Pocket *system* menu owns the
         * display — and at that point the Pocket also owns the shared data-slot
         * bridge, so an async CD read issued then wedges it and starves the GPU
         * (watchdog trap).  Detect the stall and, like Quake, freeze the voices
         * (the ring repeats the last sample) + issue no DMA until scanout
         * resumes.  Uses our own of_time_us stamp of the last change, so it
         * makes no cross-clock assumption about present timestamps. */
        of_video_get_timing(&vt);
        if (vt.present_count != pcm_present_count)
        {
            pcm_present_count = vt.present_count;
            pcm_present_change_us = now_us;
        }
        present_live = (uint32_t)(now_us - pcm_present_change_us) < PRESENT_STALE_US;

        if (!present_live)
        {
            if (!pcm_frozen)
            {
                I_PCM_DrainAsync();             /* retire any in-flight read */
                cd_fold();
                of_mixer_set_rate_h(vL, 0);     /* freeze cursors -> ring repeats */
                of_mixer_set_rate_h(vR, 0);
                pcm_frozen = 1;
            }
            I_OpenFPGAMixerPump();
            return;                             /* no DMA while the OSD owns the bridge */
        }
        if (pcm_frozen)
        {
            of_mixer_set_rate_h(vL, MUS_RATE);  /* scanout resumed — unfreeze */
            of_mixer_set_rate_h(vR, MUS_RATE);
            pcm_frozen = 0;
        }

        /* While a menu is up, never issue async CD reads: the single data-slot
         * DMA bridge is shared with the save/config slot I/O the menu triggers,
         * and a CD read in flight there wedges it (= hang on Load/Save Game).
         * Retire any in-flight read on menu open and refill synchronously (plain
         * file reads, no DMA bridge); hand back to async when the menu closes. */
        if (cd_async_ok)
        {
            if (quiet && !cd_menu_prev)
            {
                I_PCM_DrainAsync();
                cd_fold();
                pcm_rd = cd_read_off;   /* sync resumes where async left off */
            }
            else if (!quiet && cd_menu_prev)
            {
                cd_read_off = pcm_rd;   /* async resumes where sync left off */
            }
        }
        cd_menu_prev = quiet;

        pos = of_mixer_get_position_h(vL);
        if (pos >= 0)
        {
            consumed = (pos - last_pos + RING_FRAMES) % RING_FRAMES;
            if (consumed >= RING_FRAMES)
                consumed = RING_FRAMES - 1;
            last_pos = pos;

            if (!pcm_paused)
            {
                int vol = pcm_group_volume();
                if (vol != last_vol)
                {
                    of_mixer_set_group_volume(OF_MIXER_GROUP_MUSIC, vol);
                    last_vol = vol;
                }
            }

            ring_valid -= consumed;
            if (ring_valid < 0)
                ring_valid = 0;

            if (cd_async_ok && !quiet)
            {
                /* Issue the next read once the cursor freed a whole chunk. */
                if (!cd_pending && !pcm_ended &&
                    RING_FRAMES - ring_valid >= cd_chunk)
                    cd_issue();

                if (!cd_async_ok)            /* async dropped mid-stream */
                    pcm_rd = cd_read_off;    /* resync the sync cursor */
            }
            else if (consumed > 0)
            {
                pcm_produce(consumed);       /* sync top-up (menu up, or no async) */
            }

            if (pcm_ended && ring_valid <= 0)
                I_PCM_Stop();
        }
    }

    I_OpenFPGAMixerPump();
}

void I_PCM_SetVolume(int volume)
{
    if (volume < 0) volume = 0; else if (volume > 127) volume = 127;
    pcm_volume = volume;
}

void I_PCM_Pause(void)
{
    if (pcm_playing && !pcm_paused)
    {
        of_mixer_set_group_volume(OF_MIXER_GROUP_MUSIC, 0);
        pcm_paused = 1;
    }
}

void I_PCM_Resume(void)
{
    if (pcm_playing && pcm_paused)
    {
        last_vol = pcm_group_volume();
        of_mixer_set_group_volume(OF_MIXER_GROUP_MUSIC, last_vol);
        pcm_paused = 0;
    }
}

#else

typedef int i_pcmmusic_translation_unit_not_empty;

#endif /* OF_DOOM */
