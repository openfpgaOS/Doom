/*
 * of_sdl2.c -- SDL2 backend for openfpgaOS Application API
 *
 * Implements the full of.h API using SDL2 so games can be built
 * and tested on a PC (Linux/macOS/Windows).
 *
 * Build: cc -DOF_PC app.c of_sdl2.c $(sdl2-config --cflags --libs) -lm
 */

#ifndef OF_PC
#define OF_PC
#endif
#define OF_NO_COMPAT
#include "of.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ======================================================================
 * Internal state
 * ====================================================================== */

static SDL_Window   *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture  *g_texture;

/* Double-buffered 320x240 framebuffers (largest supported format = 16bpp) */
static uint8_t  g_fb[2][OF_SCREEN_W * OF_SCREEN_H * 2];
static int      g_draw_buf;           /* index of current draw buffer */

/* Color / display mode */
static int      g_color_mode   = OF_VIDEO_MODE_8BIT;
static int      g_display_mode = OF_DISPLAY_FRAMEBUFFER;
static SDL_PixelFormatEnum g_tex_format = SDL_PIXELFORMAT_ARGB8888;

/* Palette: 256 entries, 0x00RRGGBB */
static uint32_t g_palette[256];

/* Composited ARGB output (uploaded to texture) */
static uint32_t g_pixels[OF_SCREEN_W * OF_SCREEN_H];

/* Callbacks */
static void (*g_vsync_cb)(void);

/* ---- Tile engine state ---- */
static int      g_tile_enabled;
static int      g_tile_priority;  /* 0=behind FB, 1=over FB */
static int      g_tile_scroll_x;
static int      g_tile_scroll_y;
static uint16_t g_tilemap[64 * 32];          /* 64 cols x 32 rows */
static uint32_t g_tile_chr[256 * 8];         /* 256 tiles x 8 rows x 32-bit */

/* ---- Sprite engine state ---- */
#define MAX_SPRITES 64

typedef struct {
    int16_t  x, y;
    uint8_t  tile_id;
    uint8_t  palette;
    uint8_t  hflip, vflip;
    uint8_t  enabled;
} sprite_t;

static int      g_sprite_enabled;
static sprite_t g_sprites[MAX_SPRITES];
static uint32_t g_sprite_chr[256 * 8];       /* same format as tile chr */

/* ---- Input state ---- */
static of_input_state_t g_input[2];
static uint32_t g_prev_buttons[2];
static SDL_GameController *g_pads[2];

/* ---- Audio / mixer state ---- */
#define MIX_RATE        OF_AUDIO_RATE
#define MIX_VOICES      OF_MIXER_MAX_VOICES     /* 32 */
#define MIX_GROUPS      4
#define STREAM_RING     (1 << 17)               /* 128 KiB of int16 samples */
#define RAW_RING        (1 << 15)               /* 32 KiB of int16 samples */

typedef struct {
    const int16_t *pcm;             /* non-NULL if playing */
    uint32_t  sample_count;         /* length in mono samples */
    int32_t   loop_start, loop_end; /* loop_end < 0 => no loop */
    uint32_t  pos_frac;             /* 16.16 position within pcm */
    uint32_t  rate_fp16;            /* 16.16 playback rate (pcm_rate / mix_rate) */
    uint16_t  vol_l, vol_r;         /* 0..255 linear */
    uint16_t  vol_target_l, vol_target_r;
    uint16_t  vol_ramp;             /* step per sample (0 = snap) */
    uint8_t   group;                /* 0..3 */
    uint8_t   active;               /* 1 if producing output */
    uint8_t   bidi;
    int8_t    dir;                  /* +1 forward, -1 reverse (bidi) */
    uint8_t   owns_pcm;             /* 1 if we allocated pcm */
    int16_t   priority;
} voice_t;

static voice_t          g_voices[MIX_VOICES];
static uint16_t         g_group_vol[MIX_GROUPS] = { 255, 255, 255, 255 };
static uint16_t         g_master_vol = 255;
static SDL_AudioDeviceID g_audio_dev;
static SDL_mutex       *g_audio_mutex;
static int              g_audio_initialized;

/* Raw "audio_write" FIFO — merged into mixer output. */
static int16_t          g_raw_ring[RAW_RING];     /* interleaved stereo */
static volatile int     g_raw_read, g_raw_write;  /* frame indices / stride=2 */

/* Streaming audio (music, voice). */
static int16_t          g_stream_ring[STREAM_RING]; /* interleaved stereo */
static volatile int     g_stream_read, g_stream_write;
static int              g_stream_rate;             /* sample rate of source */
static uint32_t         g_stream_rate_fp16;        /* resample ratio */
static uint32_t         g_stream_pos_frac;         /* 16.16 frac */
static int              g_stream_open;

/* Completion signaling for of_mixer_poll_ended */
static volatile uint32_t g_ended_mask;
static void (*g_end_cb)(uint32_t);

/* ---- Timer ---- */
static uint64_t g_start_us;

static uint64_t get_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

/* ======================================================================
 * Video
 * ====================================================================== */

/* Render tile layer into a scanline buffer (palette indices) */
static void render_tile_scanline(uint8_t *line, int y) {
    int sy = (y + g_tile_scroll_y) & 0xFF;  /* 256 pixel wrap (32 tiles * 8) */
    int tile_row = sy >> 3;
    int fine_y   = sy & 7;

    for (int x = 0; x < OF_SCREEN_W; x++) {
        int sx = (x + g_tile_scroll_x) & 0x1FF;  /* 512 pixel wrap (64 * 8) */
        int tile_col = sx >> 3;
        int fine_x   = sx & 7;

        uint16_t entry = g_tilemap[tile_row * 64 + tile_col];
        int tile_id = entry & 0xFF;
        int pal     = (entry >> 10) & 0xF;
        int hflip   = (entry >> 14) & 1;
        int vflip   = (entry >> 15) & 1;

        int row = vflip ? (7 - fine_y) : fine_y;
        int col = hflip ? (7 - fine_x) : fine_x;

        uint32_t chr_word = g_tile_chr[tile_id * 8 + row];
        int nibble = (chr_word >> (col * 4)) & 0xF;

        line[x] = nibble ? ((pal << 4) | nibble) : 0;
    }
}

/* Render all sprites into a scanline buffer (palette indices) */
static void render_sprite_scanline(uint8_t *line, int y) {
    memset(line, 0, OF_SCREEN_W);

    /* Back to front for correct priority (sprite 0 = highest) */
    for (int i = MAX_SPRITES - 1; i >= 0; i--) {
        sprite_t *s = &g_sprites[i];
        if (!s->enabled) continue;

        int sy = y - s->y;
        if (sy < 0 || sy >= 8) continue;

        int row = s->vflip ? (7 - sy) : sy;
        uint32_t chr_word = g_sprite_chr[s->tile_id * 8 + row];

        for (int px = 0; px < 8; px++) {
            int col = s->hflip ? (7 - px) : px;
            int nibble = (chr_word >> (col * 4)) & 0xF;
            if (nibble == 0) continue;

            int screen_x = s->x + px;
            if (screen_x < 0 || screen_x >= OF_SCREEN_W) continue;

            line[screen_x] = (s->palette << 4) | nibble;
        }
    }
}

static inline uint32_t expand_rgb565(uint16_t p) {
    uint32_t r = (p >> 11) & 0x1F;
    uint32_t g = (p >>  5) & 0x3F;
    uint32_t b =  p        & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static inline uint32_t expand_rgb555(uint16_t p) {
    uint32_t r = (p >> 10) & 0x1F;
    uint32_t g = (p >>  5) & 0x1F;
    uint32_t b =  p        & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static inline uint32_t expand_rgba5551(uint16_t p) {
    if (!(p & 1)) return 0;  /* alpha bit 0 => transparent */
    uint32_t r = (p >> 11) & 0x1F;
    uint32_t g = (p >>  6) & 0x1F;
    uint32_t b = (p >>  1) & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* Composite all layers and upload to texture */
static void composite_and_present(void) {
    int disp = g_draw_buf ^ 1;
    const uint8_t *fb_bytes = g_fb[disp];
    uint8_t tile_line[OF_SCREEN_W];
    uint8_t sprite_line[OF_SCREEN_W];

    for (int y = 0; y < OF_SCREEN_H; y++) {
        if (g_tile_enabled)    render_tile_scanline(tile_line, y);
        if (g_sprite_enabled)  render_sprite_scanline(sprite_line, y);

        uint32_t *dst = &g_pixels[y * OF_SCREEN_W];
        const uint8_t  *fb8   = fb_bytes + y * OF_SCREEN_W;
        const uint16_t *fb16  = (const uint16_t *)(fb_bytes + y * OF_SCREEN_W * 2);

        for (int x = 0; x < OF_SCREEN_W; x++) {
            uint32_t color = 0xFF000000;

            /* Sprite and tile-high layers take priority */
            if (g_sprite_enabled && sprite_line[x]) {
                color = g_palette[sprite_line[x]] | 0xFF000000;
            } else if (g_tile_enabled && g_tile_priority && tile_line[x]) {
                color = g_palette[tile_line[x]] | 0xFF000000;
            } else {
                uint32_t fbc = 0;
                int     fb_opaque = 0;
                switch (g_color_mode) {
                case OF_VIDEO_MODE_8BIT: {
                    uint8_t idx = fb8[x];
                    fbc = g_palette[idx] | 0xFF000000;
                    fb_opaque = (idx != 0);
                    break;
                }
                case OF_VIDEO_MODE_4BIT: {
                    uint8_t byte = fb8[x >> 1];
                    uint8_t idx  = (x & 1) ? (byte >> 4) : (byte & 0xF);
                    fbc = g_palette[idx] | 0xFF000000;
                    fb_opaque = (idx != 0);
                    break;
                }
                case OF_VIDEO_MODE_2BIT: {
                    uint8_t byte = fb8[x >> 2];
                    uint8_t idx  = (byte >> ((x & 3) * 2)) & 0x3;
                    fbc = g_palette[idx] | 0xFF000000;
                    fb_opaque = (idx != 0);
                    break;
                }
                case OF_VIDEO_MODE_RGB565:
                    fbc = expand_rgb565(fb16[x]);
                    fb_opaque = (fb16[x] != 0);
                    break;
                case OF_VIDEO_MODE_RGB555:
                    fbc = expand_rgb555(fb16[x]);
                    fb_opaque = (fb16[x] != 0);
                    break;
                case OF_VIDEO_MODE_RGBA5551:
                    fbc = expand_rgba5551(fb16[x]);
                    fb_opaque = (fbc != 0);
                    break;
                }

                if (fb_opaque) {
                    color = fbc;
                } else if (g_tile_enabled && !g_tile_priority && tile_line[x]) {
                    color = g_palette[tile_line[x]] | 0xFF000000;
                }
            }

            dst[x] = color;
        }
    }

    SDL_UpdateTexture(g_texture, NULL, g_pixels, OF_SCREEN_W * 4);
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, NULL, NULL);
    SDL_RenderPresent(g_renderer);

    if (g_vsync_cb) g_vsync_cb();
}

void of_video_init(void) {
    if (!g_window) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            exit(1);
        }
        g_start_us = get_us();

        g_window = SDL_CreateWindow("openfpgaOS",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            OF_SCREEN_W * 3, OF_SCREEN_H * 3,
            SDL_WINDOW_RESIZABLE);
        /* VSync default: on. Apps that pace themselves (e.g. 35 Hz Doom
         * on a 60 Hz display, where hard vsync would snap each frame
         * to a 16.67 ms boundary and effectively render at 30 Hz) can
         * disable it by exporting OF_NO_VSYNC=1 before of_video_init.
         * Tic-rate pacing from the app then drives the cadence. */
        const char *no_vsync = getenv("OF_NO_VSYNC");
        uint32_t rflags = SDL_RENDERER_ACCELERATED;
        if (!no_vsync || no_vsync[0] == '0')
            rflags |= SDL_RENDERER_PRESENTVSYNC;
        g_renderer = SDL_CreateRenderer(g_window, -1, rflags);
        SDL_RenderSetLogicalSize(g_renderer, OF_SCREEN_W, OF_SCREEN_H);
        SDL_RenderSetIntegerScale(g_renderer, SDL_TRUE);
        g_texture = SDL_CreateTexture(g_renderer,
            g_tex_format, SDL_TEXTUREACCESS_STREAMING,
            OF_SCREEN_W, OF_SCREEN_H);
    }

    memset(g_fb, 0, sizeof(g_fb));
    g_draw_buf = 0;
    memset(g_palette, 0, sizeof(g_palette));
}

uint8_t *of_video_surface(void) {
    return g_fb[g_draw_buf];
}

void of_video_flip(void) {
    composite_and_present();
    g_draw_buf ^= 1;
}

void of_video_wait_flip(void) {
    /* PRESENTVSYNC makes RenderPresent block until swap; flip already waits. */
}

void of_video_clear(uint8_t color) {
    int bytes = OF_SCREEN_W * OF_SCREEN_H;
    switch (g_color_mode) {
    case OF_VIDEO_MODE_4BIT:  bytes /= 2;  break;
    case OF_VIDEO_MODE_2BIT:  bytes /= 4;  break;
    case OF_VIDEO_MODE_RGB565:
    case OF_VIDEO_MODE_RGB555:
    case OF_VIDEO_MODE_RGBA5551: bytes *= 2; break;
    default: break;
    }
    memset(g_fb[g_draw_buf], color, bytes);
}

void of_video_palette(uint8_t index, uint32_t rgb) {
    g_palette[index] = rgb & 0x00FFFFFF;
}

void of_video_palette_bulk(const uint32_t *pal, int count) {
    if (count > 256) count = 256;
    memcpy(g_palette, pal, count * sizeof(uint32_t));
}

/* Set a VGA 4-byte palette (BUILD/Quake/DOOM-ish format).
 * Input is 4 bytes per entry (B, G, R, pad) in 6-bit 0..63 range. */
void of_video_palette_vga4(const uint8_t *bgra6, int count) {
    if (count > 256) count = 256;
    for (int i = 0; i < count; i++) {
        uint8_t b6 = bgra6[i*4+0] & 0x3F;
        uint8_t g6 = bgra6[i*4+1] & 0x3F;
        uint8_t r6 = bgra6[i*4+2] & 0x3F;
        uint32_t r = (r6 << 2) | (r6 >> 4);
        uint32_t g = (g6 << 2) | (g6 >> 4);
        uint32_t b = (b6 << 2) | (b6 >> 4);
        g_palette[i] = (r << 16) | (g << 8) | b;
    }
}

void of_video_flush(void) {
    /* no-op on PC */
}

void of_video_set_display_mode(int mode) {
    g_display_mode = mode;
}

void of_video_set_color_mode(int mode) {
    g_color_mode = mode;
    memset(g_fb, 0, sizeof(g_fb));
}

void of_video_set_vsync_callback(void (*cb)(void)) {
    g_vsync_cb = cb;
}

uint16_t *of_video_surface16(void) {
    return (uint16_t *)g_fb[g_draw_buf];
}

/* ======================================================================
 * Timer
 *
 * of_timer.h declares of_time_us / of_time_ms as plain externs in the
 * OF_PC build (no inline syscalls). The PC backend implements them
 * here on top of SDL_GetPerformanceCounter / SDL_GetTicks. Both are
 * monotonic and free-running, matching the on-target semantics.
 * ====================================================================== */

unsigned int of_time_us(void) {
    static Uint64 freq;
    if (!freq) freq = SDL_GetPerformanceFrequency();
    Uint64 ticks = SDL_GetPerformanceCounter();
    /* Scale to microseconds. Wraps in ~71 minutes (uint32 us), same
     * cadence as the hardware free-running timer. */
    return (unsigned int)((ticks * 1000000ULL) / freq);
}

unsigned int of_time_ms(void) {
    return (unsigned int)SDL_GetTicks();
}

/* ======================================================================
 * Input
 * ====================================================================== */

/* SDL scancode -> button mask mapping */
static uint32_t key_to_btn(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_UP:     return OF_BTN_UP;
        case SDL_SCANCODE_DOWN:   return OF_BTN_DOWN;
        case SDL_SCANCODE_LEFT:   return OF_BTN_LEFT;
        case SDL_SCANCODE_RIGHT:  return OF_BTN_RIGHT;
        case SDL_SCANCODE_Z:      return OF_BTN_A;
        case SDL_SCANCODE_X:      return OF_BTN_B;
        case SDL_SCANCODE_A:      return OF_BTN_X;
        case SDL_SCANCODE_S:      return OF_BTN_Y;
        case SDL_SCANCODE_Q:      return OF_BTN_L1;
        case SDL_SCANCODE_W:      return OF_BTN_R1;
        case SDL_SCANCODE_1:      return OF_BTN_L2;
        case SDL_SCANCODE_2:      return OF_BTN_R2;
        case SDL_SCANCODE_RSHIFT: return OF_BTN_SELECT;
        case SDL_SCANCODE_RETURN: return OF_BTN_START;
        default: return 0;
    }
}

static void poll_controllers(void) {
    for (int p = 0; p < 2; p++) {
        SDL_GameController *c = g_pads[p];
        if (!c) { g_input[p].joy_lx = g_input[p].joy_ly = 0;
                  g_input[p].joy_rx = g_input[p].joy_ry = 0;
                  g_input[p].trigger_l = g_input[p].trigger_r = 0;
                  continue; }
        g_input[p].joy_lx    = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
        g_input[p].joy_ly    = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
        g_input[p].joy_rx    = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX);
        g_input[p].joy_ry    = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY);
        g_input[p].trigger_l = (uint16_t)SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        g_input[p].trigger_r = (uint16_t)SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    }
}

void of_input_poll(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            of_exit();
            break;
        case SDL_KEYDOWN:
            if (!ev.key.repeat)
                g_input[0].buttons |= key_to_btn(ev.key.keysym.scancode);
            if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE && !ev.key.repeat)
                g_input[0].buttons |= OF_BTN_SELECT;
            break;
        case SDL_KEYUP:
            g_input[0].buttons &= ~key_to_btn(ev.key.keysym.scancode);
            if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                g_input[0].buttons &= ~OF_BTN_SELECT;
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            int p = -1;
            for (int i = 0; i < 2; i++)
                if (g_pads[i] && SDL_GameControllerFromInstanceID(ev.cbutton.which) == g_pads[i]) { p = i; break; }
            if (p < 0) break;
            uint32_t mask = 0;
            switch (ev.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:    mask = OF_BTN_UP; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  mask = OF_BTN_DOWN; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  mask = OF_BTN_LEFT; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: mask = OF_BTN_RIGHT; break;
                case SDL_CONTROLLER_BUTTON_A:          mask = OF_BTN_A; break;
                case SDL_CONTROLLER_BUTTON_B:          mask = OF_BTN_B; break;
                case SDL_CONTROLLER_BUTTON_X:          mask = OF_BTN_X; break;
                case SDL_CONTROLLER_BUTTON_Y:          mask = OF_BTN_Y; break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  mask = OF_BTN_L1; break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: mask = OF_BTN_R1; break;
                case SDL_CONTROLLER_BUTTON_LEFTSTICK:  mask = OF_BTN_L3; break;
                case SDL_CONTROLLER_BUTTON_RIGHTSTICK: mask = OF_BTN_R3; break;
                case SDL_CONTROLLER_BUTTON_BACK:       mask = OF_BTN_SELECT; break;
                case SDL_CONTROLLER_BUTTON_START:      mask = OF_BTN_START; break;
                default: break;
            }
            if (ev.type == SDL_CONTROLLERBUTTONDOWN)
                g_input[p].buttons |= mask;
            else
                g_input[p].buttons &= ~mask;
            break;
        }
        case SDL_CONTROLLERDEVICEADDED: {
            SDL_GameController *c = SDL_GameControllerOpen(ev.cdevice.which);
            if (!c) break;
            int slot = g_pads[0] ? 1 : 0;
            if (!g_pads[slot]) g_pads[slot] = c;
            else SDL_GameControllerClose(c);
            break;
        }
        case SDL_CONTROLLERDEVICEREMOVED:
            for (int i = 0; i < 2; i++) {
                if (g_pads[i] &&
                    SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[i])) == ev.cdevice.which) {
                    SDL_GameControllerClose(g_pads[i]);
                    g_pads[i] = NULL;
                }
            }
            break;
        }
    }

    poll_controllers();

    /* Compute pressed/released edges */
    for (int p = 0; p < 2; p++) {
        g_input[p].buttons_pressed  = g_input[p].buttons & ~g_prev_buttons[p];
        g_input[p].buttons_released = ~g_input[p].buttons & g_prev_buttons[p];
        g_prev_buttons[p] = g_input[p].buttons;
    }
}

int of_btn(uint32_t mask)          { return (g_input[0].buttons & mask) != 0; }
int of_btn_pressed(uint32_t mask)  { return (g_input[0].buttons_pressed & mask) != 0; }
int of_btn_released(uint32_t mask) { return (g_input[0].buttons_released & mask) != 0; }
int of_btn_p2(uint32_t mask)           { return (g_input[1].buttons & mask) != 0; }
int of_btn_pressed_p2(uint32_t mask)   { return (g_input[1].buttons_pressed & mask) != 0; }
int of_btn_released_p2(uint32_t mask)  { return (g_input[1].buttons_released & mask) != 0; }

uint32_t of_input_state(int player, of_input_state_t *state) {
    if (player >= 0 && player < 2 && state)
        *state = g_input[player];
    return (player >= 0 && player < 2) ? g_input[player].buttons : 0;
}

void of_input_set_deadzone(int16_t deadzone) { (void)deadzone; }

/* ======================================================================
 * Audio / Mixer
 *
 *   - of_audio_write() pushes stereo s16 frames onto a raw ring; these
 *     are summed with the mixer output in the callback.
 *   - of_mixer_play() allocates a voice, stores PCM pointer, sets rate.
 *   - of_mixer_pump() is a no-op (mixing happens in the audio callback).
 *   - of_audio_stream_* uses a large ring; rate-converts to MIX_RATE.
 *   - MIDI is stubbed but settable (of_midi_set_volume works).
 * ====================================================================== */

/* ---- Voice helpers ---------------------------------------------------- */

static int alloc_voice(int priority) {
    /* Prefer inactive voices */
    for (int i = 0; i < MIX_VOICES; i++)
        if (!g_voices[i].active) return i;
    /* Evict lowest-priority active voice */
    int evict = -1;
    int16_t lowest = INT16_MAX;
    for (int i = 0; i < MIX_VOICES; i++) {
        if (g_voices[i].priority < lowest) { lowest = g_voices[i].priority; evict = i; }
    }
    if (evict >= 0 && priority >= lowest) return evict;
    return -1;
}

static void voice_reset(voice_t *v) {
    memset(v, 0, sizeof(*v));
    v->dir = 1;
    v->loop_end = -1;
    v->vol_ramp = 0;
}

/* ---- Audio callback (mixer core) -------------------------------------- */

static inline int16_t sat16(int32_t x) {
    if (x >  32767) return  32767;
    if (x < -32768) return -32768;
    return (int16_t)x;
}

static void audio_callback(void *user, uint8_t *stream, int len) {
    (void)user;
    int16_t *out = (int16_t *)stream;
    int frames = len / 4;    /* stereo s16 */

    SDL_LockMutex(g_audio_mutex);

    uint32_t ended = 0;

    for (int f = 0; f < frames; f++) {
        int32_t accL = 0, accR = 0;

        /* ---- raw ring (of_audio_write) -- interleaved stereo -------- */
        if (g_raw_read != g_raw_write) {
            accL += g_raw_ring[(g_raw_read * 2 + 0) & (RAW_RING - 1)];
            accR += g_raw_ring[(g_raw_read * 2 + 1) & (RAW_RING - 1)];
            g_raw_read = (g_raw_read + 1) & ((RAW_RING / 2) - 1);
        }

        /* ---- streaming (music) -------------------------------------- */
        if (g_stream_open && g_stream_rate > 0) {
            /* Resample: consume g_stream_rate samples per MIX_RATE */
            int avail = (g_stream_write - g_stream_read) & (STREAM_RING - 1);
            if (avail >= 4) {
                int idx = g_stream_read;
                accL += g_stream_ring[(idx + 0) & (STREAM_RING - 1)];
                accR += g_stream_ring[(idx + 1) & (STREAM_RING - 1)];
                g_stream_pos_frac += g_stream_rate_fp16;
                while (g_stream_pos_frac >= 0x10000) {
                    g_stream_pos_frac -= 0x10000;
                    if (((g_stream_write - g_stream_read) & (STREAM_RING - 1)) >= 4)
                        g_stream_read = (g_stream_read + 2) & (STREAM_RING - 1);
                }
            }
        }

        /* ---- mixer voices ------------------------------------------- */
        for (int vi = 0; vi < MIX_VOICES; vi++) {
            voice_t *v = &g_voices[vi];
            if (!v->active) continue;

            uint32_t pos_i = v->pos_frac >> 16;
            if (pos_i >= v->sample_count) {
                v->active = 0;
                ended |= (1u << vi);
                continue;
            }
            int32_t s = v->pcm[pos_i];

            /* Apply volume (voice × group × master) — 0..255 each => shift 24 */
            int32_t gL = (int32_t)v->vol_l * g_group_vol[v->group] * g_master_vol;
            int32_t gR = (int32_t)v->vol_r * g_group_vol[v->group] * g_master_vol;
            accL += (int32_t)((int64_t)s * gL >> 24);
            accR += (int32_t)((int64_t)s * gR >> 24);

            /* Volume ramp toward target */
            if (v->vol_l != v->vol_target_l) {
                int step = v->vol_ramp ? v->vol_ramp : 8;
                if (v->vol_l < v->vol_target_l)
                    v->vol_l = (v->vol_l + step > v->vol_target_l) ? v->vol_target_l : v->vol_l + step;
                else
                    v->vol_l = (v->vol_l < step || v->vol_l - step < v->vol_target_l) ? v->vol_target_l : v->vol_l - step;
            }
            if (v->vol_r != v->vol_target_r) {
                int step = v->vol_ramp ? v->vol_ramp : 8;
                if (v->vol_r < v->vol_target_r)
                    v->vol_r = (v->vol_r + step > v->vol_target_r) ? v->vol_target_r : v->vol_r + step;
                else
                    v->vol_r = (v->vol_r < step || v->vol_r - step < v->vol_target_r) ? v->vol_target_r : v->vol_r - step;
            }

            /* Advance position */
            if (v->dir >= 0) {
                v->pos_frac += v->rate_fp16;
                pos_i = v->pos_frac >> 16;
                if (v->loop_end >= 0 && (int32_t)pos_i >= v->loop_end) {
                    if (v->bidi) {
                        v->dir = -1;
                        v->pos_frac = ((uint32_t)v->loop_end << 16);
                    } else {
                        v->pos_frac = ((uint32_t)v->loop_start << 16) + (v->pos_frac - ((uint32_t)v->loop_end << 16));
                    }
                } else if (pos_i >= v->sample_count) {
                    v->active = 0;
                    ended |= (1u << vi);
                }
            } else {
                if (v->pos_frac < v->rate_fp16) {
                    v->dir = 1;
                    v->pos_frac = ((uint32_t)v->loop_start << 16);
                } else {
                    v->pos_frac -= v->rate_fp16;
                }
            }
        }

        out[f * 2 + 0] = sat16(accL);
        out[f * 2 + 1] = sat16(accR);
    }

    if (ended) {
        g_ended_mask |= ended;
        if (g_end_cb) g_end_cb(ended);
    }

    SDL_UnlockMutex(g_audio_mutex);
}

void of_audio_init(void) {
    if (g_audio_initialized) return;
    g_audio_initialized = 1;

    if (!SDL_WasInit(SDL_INIT_AUDIO)) SDL_InitSubSystem(SDL_INIT_AUDIO);
    g_audio_mutex = SDL_CreateMutex();

    for (int i = 0; i < MIX_VOICES; i++) voice_reset(&g_voices[i]);

    SDL_AudioSpec want = {0}, have;
    want.freq = MIX_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_callback;

    g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have,
                                      SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (g_audio_dev)
        SDL_PauseAudioDevice(g_audio_dev, 0);
}

int of_audio_write(const int16_t *samples, int count) {
    if (!g_audio_dev) of_audio_init();
    int written = 0;
    SDL_LockMutex(g_audio_mutex);
    for (int i = 0; i < count; i++) {
        int next = (g_raw_write + 1) & ((RAW_RING / 2) - 1);
        if (next == g_raw_read) break;
        g_raw_ring[(g_raw_write * 2 + 0) & (RAW_RING - 1)] = samples[i * 2 + 0];
        g_raw_ring[(g_raw_write * 2 + 1) & (RAW_RING - 1)] = samples[i * 2 + 1];
        g_raw_write = next;
        written++;
    }
    SDL_UnlockMutex(g_audio_mutex);
    return written;
}

int of_audio_free(void) {
    SDL_LockMutex(g_audio_mutex);
    int used = (g_raw_write - g_raw_read) & ((RAW_RING / 2) - 1);
    int freef = (RAW_RING / 2) - 1 - used;
    SDL_UnlockMutex(g_audio_mutex);
    return freef;
}

/* ---- Streaming -------------------------------------------------------- */

int of_audio_stream_open(int sample_rate) {
    if (!g_audio_dev) of_audio_init();
    SDL_LockMutex(g_audio_mutex);
    g_stream_rate      = sample_rate;
    g_stream_rate_fp16 = (uint32_t)(((uint64_t)sample_rate << 16) / MIX_RATE);
    g_stream_pos_frac  = 0;
    g_stream_read = g_stream_write = 0;
    g_stream_open = 1;
    SDL_UnlockMutex(g_audio_mutex);
    return 0;
}

int of_audio_stream_write(const int16_t *samples, int count) {
    if (!g_stream_open) return -1;
    int written = 0;
    SDL_LockMutex(g_audio_mutex);
    for (int i = 0; i < count; i++) {
        int next = (g_stream_write + 2) & (STREAM_RING - 1);
        if (next == g_stream_read) break;   /* full */
        g_stream_ring[g_stream_write] = samples[i*2 + 0];
        g_stream_ring[(g_stream_write + 1) & (STREAM_RING - 1)] = samples[i*2 + 1];
        g_stream_write = next;
        written++;
    }
    SDL_UnlockMutex(g_audio_mutex);
    return written;
}

int of_audio_stream_ready(void) {
    if (!g_stream_open) return 1;
    SDL_LockMutex(g_audio_mutex);
    int used = (g_stream_write - g_stream_read) & (STREAM_RING - 1);
    SDL_UnlockMutex(g_audio_mutex);
    /* "ready" when half empty or less */
    return used < STREAM_RING / 2;
}

void of_audio_stream_close(void) {
    SDL_LockMutex(g_audio_mutex);
    g_stream_open = 0;
    g_stream_read = g_stream_write = 0;
    SDL_UnlockMutex(g_audio_mutex);
}

/* ---- Mixer public API ------------------------------------------------- */

void of_mixer_init(int max_voices, int output_rate) {
    (void)max_voices; (void)output_rate;
    of_audio_init();
}

static int mixer_play_s16(const int16_t *pcm, uint32_t count, uint32_t sr,
                          int priority, int volume) {
    if (!g_audio_dev) of_audio_init();
    SDL_LockMutex(g_audio_mutex);
    int vi = alloc_voice(priority);
    if (vi >= 0) {
        voice_t *v = &g_voices[vi];
        voice_reset(v);
        v->pcm          = pcm;
        v->sample_count = count;
        v->rate_fp16    = (uint32_t)(((uint64_t)sr << 16) / MIX_RATE);
        v->vol_l = v->vol_r = v->vol_target_l = v->vol_target_r = (uint16_t)volume;
        v->priority     = (int16_t)priority;
        v->active       = 1;
        v->dir          = 1;
        v->loop_end     = -1;
    }
    SDL_UnlockMutex(g_audio_mutex);
    return vi;
}

int of_mixer_play(const uint8_t *pcm_s16, uint32_t sample_count,
                  uint32_t sample_rate, int priority, int volume) {
    return mixer_play_s16((const int16_t *)pcm_s16, sample_count,
                          sample_rate, priority, volume);
}

int of_mixer_play_8bit(const uint8_t *pcm_s8, uint32_t sample_count,
                       uint32_t sample_rate, int priority, int volume) {
    /* Expand 8-bit unsigned to 16-bit signed in a temp buffer attached to
     * a new voice.  We cache via malloc; freed when voice ends (not
     * tracked — leaks if called rapidly, good enough for demos).
     * Doom's i_sdlsound expands before calling, so this path is rare. */
    int16_t *buf = (int16_t *)malloc(sample_count * sizeof(int16_t));
    if (!buf) return -1;
    for (uint32_t i = 0; i < sample_count; i++)
        buf[i] = (int16_t)((pcm_s8[i] - 128) << 8);

    SDL_LockMutex(g_audio_mutex);
    int vi = alloc_voice(priority);
    if (vi >= 0) {
        voice_t *v = &g_voices[vi];
        voice_reset(v);
        v->pcm          = buf;
        v->sample_count = sample_count;
        v->rate_fp16    = (uint32_t)(((uint64_t)sample_rate << 16) / MIX_RATE);
        v->vol_l = v->vol_r = v->vol_target_l = v->vol_target_r = (uint16_t)volume;
        v->priority     = (int16_t)priority;
        v->active       = 1;
        v->dir          = 1;
        v->loop_end     = -1;
        v->owns_pcm     = 1;
    } else {
        free(buf);
    }
    SDL_UnlockMutex(g_audio_mutex);
    return vi;
}

void of_mixer_retrigger(int voice, const uint8_t *pcm_s16, uint32_t count,
                        uint32_t sr, int volume) {
    if ((unsigned)voice >= MIX_VOICES) return;
    SDL_LockMutex(g_audio_mutex);
    voice_t *v = &g_voices[voice];
    if (v->owns_pcm && v->pcm) { free((void *)v->pcm); v->owns_pcm = 0; }
    voice_reset(v);
    v->pcm          = (const int16_t *)pcm_s16;
    v->sample_count = count;
    v->rate_fp16    = (uint32_t)(((uint64_t)sr << 16) / MIX_RATE);
    v->vol_l = v->vol_r = v->vol_target_l = v->vol_target_r = (uint16_t)volume;
    v->active       = 1;
    v->dir          = 1;
    v->loop_end     = -1;
    SDL_UnlockMutex(g_audio_mutex);
}

void of_mixer_stop(int voice) {
    if ((unsigned)voice >= MIX_VOICES) return;
    SDL_LockMutex(g_audio_mutex);
    voice_t *v = &g_voices[voice];
    if (v->owns_pcm && v->pcm) { free((void *)v->pcm); v->owns_pcm = 0; }
    v->active = 0;
    SDL_UnlockMutex(g_audio_mutex);
}

void of_mixer_stop_all(void) {
    SDL_LockMutex(g_audio_mutex);
    for (int i = 0; i < MIX_VOICES; i++) {
        if (g_voices[i].owns_pcm && g_voices[i].pcm) free((void *)g_voices[i].pcm);
        voice_reset(&g_voices[i]);
    }
    SDL_UnlockMutex(g_audio_mutex);
}

void of_mixer_set_volume(int voice, int volume) {
    if ((unsigned)voice >= MIX_VOICES) return;
    SDL_LockMutex(g_audio_mutex);
    g_voices[voice].vol_target_l = g_voices[voice].vol_target_r = (uint16_t)volume;
    if (!g_voices[voice].vol_ramp)
        g_voices[voice].vol_l = g_voices[voice].vol_r = (uint16_t)volume;
    SDL_UnlockMutex(g_audio_mutex);
}

void of_mixer_pump(void) { /* callback drives mixing */ }

int of_mixer_voice_active(int voice) {
    if ((unsigned)voice >= MIX_VOICES) return 0;
    return g_voices[voice].active;
}

void of_mixer_set_pan(int voice, int pan) {
    if ((unsigned)voice >= MIX_VOICES) return;
    /* pan: 0 = full-left, 128 = center, 255 = full-right */
    int v = g_voices[voice].vol_target_l > g_voices[voice].vol_target_r ?
            g_voices[voice].vol_target_l : g_voices[voice].vol_target_r;
    if (!v) v = 255;
    int l = ((255 - pan) * v) / 255;
    int r = (pan * v) / 255;
    SDL_LockMutex(g_audio_mutex);
    g_voices[voice].vol_target_l = (uint16_t)l;
    g_voices[voice].vol_target_r = (uint16_t)r;
    if (!g_voices[voice].vol_ramp) {
        g_voices[voice].vol_l = (uint16_t)l;
        g_voices[voice].vol_r = (uint16_t)r;
    }
    SDL_UnlockMutex(g_audio_mutex);
}

void of_mixer_set_loop(int voice, int loop_start, int loop_end) {
    if ((unsigned)voice >= MIX_VOICES) return;
    SDL_LockMutex(g_audio_mutex);
    g_voices[voice].loop_start = loop_start;
    g_voices[voice].loop_end   = loop_end;
    SDL_UnlockMutex(g_audio_mutex);
}

void of_mixer_set_rate(int voice, int sample_rate_hz) {
    if ((unsigned)voice >= MIX_VOICES) return;
    SDL_LockMutex(g_audio_mutex);
    g_voices[voice].rate_fp16 = (uint32_t)(((uint64_t)sample_rate_hz << 16) / MIX_RATE);
    SDL_UnlockMutex(g_audio_mutex);
}

void of_mixer_set_vol_lr(int voice, int vol_l, int vol_r) {
    if ((unsigned)voice >= MIX_VOICES) return;
    SDL_LockMutex(g_audio_mutex);
    g_voices[voice].vol_target_l = (uint16_t)vol_l;
    g_voices[voice].vol_target_r = (uint16_t)vol_r;
    if (!g_voices[voice].vol_ramp) {
        g_voices[voice].vol_l = (uint16_t)vol_l;
        g_voices[voice].vol_r = (uint16_t)vol_r;
    }
    SDL_UnlockMutex(g_audio_mutex);
}

void of_mixer_set_bidi(int voice, int enable) {
    if ((unsigned)voice >= MIX_VOICES) return;
    g_voices[voice].bidi = (uint8_t)(enable ? 1 : 0);
}

int of_mixer_get_position(int voice) {
    if ((unsigned)voice >= MIX_VOICES) return 0;
    return (int)(g_voices[voice].pos_frac >> 16);
}

void of_mixer_set_position(int voice, int sample_offset) {
    if ((unsigned)voice >= MIX_VOICES) return;
    SDL_LockMutex(g_audio_mutex);
    g_voices[voice].pos_frac = (uint32_t)sample_offset << 16;
    SDL_UnlockMutex(g_audio_mutex);
}

void of_mixer_set_voice(int voice, int sample_rate_hz, int vol_l, int vol_r) {
    of_mixer_set_rate(voice, sample_rate_hz);
    of_mixer_set_vol_lr(voice, vol_l, vol_r);
}

void of_mixer_set_rate_raw(int voice, uint32_t rate_fp16) {
    if ((unsigned)voice >= MIX_VOICES) return;
    g_voices[voice].rate_fp16 = rate_fp16;
}

void of_mixer_set_voice_raw(int voice, uint32_t rate_fp16, int vol_l, int vol_r) {
    of_mixer_set_rate_raw(voice, rate_fp16);
    of_mixer_set_vol_lr(voice, vol_l, vol_r);
}

void of_mixer_set_volume_ramp(int voice, int rate) {
    if ((unsigned)voice >= MIX_VOICES) return;
    g_voices[voice].vol_ramp = (uint16_t)rate;
}

uint32_t of_mixer_poll_ended(void) {
    uint32_t m;
    SDL_LockMutex(g_audio_mutex);
    m = g_ended_mask; g_ended_mask = 0;
    SDL_UnlockMutex(g_audio_mutex);
    return m;
}

/* Sample memory allocator — we simply malloc from host heap; on FPGA this
 * comes from the CRAM1 pool. */
void *of_mixer_alloc_samples(size_t size) { return malloc(size); }
void  of_mixer_free_samples(void)          { /* host: owned by caller via free() */ }

void of_mixer_set_end_callback(void (*cb)(uint32_t)) { g_end_cb = cb; }

void of_mixer_set_group(int voice, int group) {
    if ((unsigned)voice >= MIX_VOICES || (unsigned)group >= MIX_GROUPS) return;
    g_voices[voice].group = (uint8_t)group;
}

void of_mixer_set_group_volume(int group, int volume) {
    if ((unsigned)group >= MIX_GROUPS) return;
    g_group_vol[group] = (uint16_t)volume;
}

void of_mixer_set_master_volume(int volume) {
    g_master_vol = (uint16_t)volume;
}

void of_mixer_set_filter(int voice, int cutoff_q016, int q, int enable) {
    (void)voice; (void)cutoff_q016; (void)q; (void)enable;
    /* Filter not modeled on PC — no-op. */
}

void of_mixer_commit(void) {
    /* Target uses double-buffered voice slots; on PC the mutex in the
     * audio callback provides the same atomicity, so commit is a no-op. */
}

/* MIDI playback: provided by of_midi.c (shared across targets). */

/* ======================================================================
 * File I/O
 *
 * On PC, data slot files are read from ./data/<slot_id>.bin
 * Set OF_DATA_DIR env var to override.
 * ====================================================================== */

int of_file_read(uint32_t slot_id, uint32_t offset, void *dest, uint32_t length) {
    char path[512];
    const char *dir = getenv("OF_DATA_DIR");
    if (!dir) dir = "data";
    snprintf(path, sizeof(path), "%s/%u.bin", dir, slot_id);

    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, offset, SEEK_SET);
    int n = fread(dest, 1, length, f);
    fclose(f);
    return n == (int)length ? 0 : -1;
}

long of_file_size(uint32_t slot_id) {
    char path[512];
    const char *dir = getenv("OF_DATA_DIR");
    if (!dir) dir = "data";
    snprintf(path, sizeof(path), "%s/%u.bin", dir, slot_id);

    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return sz;
}

/* ======================================================================
 * Link Cable (stubs)
 * ====================================================================== */

int      of_link_send(uint32_t data)   { (void)data; return -1; }
int      of_link_recv(uint32_t *data)  { (void)data; return -1; }
uint32_t of_link_status(void)          { return 0; }


/* ======================================================================
 * Analogizer (stubs)
 * ====================================================================== */

int of_analogizer_enabled(void)                     { return 0; }
int of_analogizer_state(of_analogizer_state_t *state) {
    if (state) memset(state, 0, sizeof(*state));
    return 0;
}

/* ======================================================================
 * Tile Layer
 * ====================================================================== */

void of_tile_enable(int enable, int priority) {
    g_tile_enabled  = enable;
    g_tile_priority = priority;
}

void of_tile_scroll(int x, int y) {
    g_tile_scroll_x = x;
    g_tile_scroll_y = y;
}

void of_tile_set(int col, int row, uint16_t entry) {
    if ((unsigned)col < 64 && (unsigned)row < 32)
        g_tilemap[row * 64 + col] = entry;
}

void of_tile_load_map(const uint16_t *data, int x, int y, int w, int h) {
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            of_tile_set(x + c, y + r, data[r * w + c]);
}

void of_tile_load_chr(int first_tile, const void *data, int num_tiles) {
    int words = num_tiles * 8;
    int offset = first_tile * 8;
    if (offset + words > 256 * 8) words = 256 * 8 - offset;
    memcpy(&g_tile_chr[offset], data, words * sizeof(uint32_t));
}

/* ======================================================================
 * Sprite Engine
 * ====================================================================== */

void of_sprite_enable(int enable) {
    g_sprite_enabled = enable;
}

void of_sprite_set(int index, int x, int y, int tile_id, int palette,
                   int hflip, int vflip, int enable) {
    if ((unsigned)index >= MAX_SPRITES) return;
    sprite_t *s = &g_sprites[index];
    s->x       = (int16_t)x;
    s->y       = (int16_t)y;
    s->tile_id = (uint8_t)tile_id;
    s->palette = (uint8_t)palette;
    s->hflip   = (uint8_t)hflip;
    s->vflip   = (uint8_t)vflip;
    s->enabled = (uint8_t)enable;
}

void of_sprite_move(int index, int x, int y) {
    if ((unsigned)index >= MAX_SPRITES) return;
    g_sprites[index].x = (int16_t)x;
    g_sprites[index].y = (int16_t)y;
}

void of_sprite_load_chr(int first_tile, const void *data, int num_tiles) {
    int words = num_tiles * 8;
    int offset = first_tile * 8;
    if (offset + words > 256 * 8) words = 256 * 8 - offset;
    memcpy(&g_sprite_chr[offset], data, words * sizeof(uint32_t));
}

void of_sprite_hide(int index) {
    if ((unsigned)index < MAX_SPRITES)
        g_sprites[index].enabled = 0;
}

void of_sprite_hide_all(void) {
    for (int i = 0; i < MAX_SPRITES; i++)
        g_sprites[i].enabled = 0;
}

/* ======================================================================
 * System
 * ====================================================================== */

void of_exit(void) {
    if (g_audio_dev) {
        SDL_PauseAudioDevice(g_audio_dev, 1);
        SDL_CloseAudioDevice(g_audio_dev);
    }
    if (g_texture)  SDL_DestroyTexture(g_texture);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window)   SDL_DestroyWindow(g_window);
    SDL_Quit();
    exit(0);
}
