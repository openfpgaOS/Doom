/* txt_main.h — openfpgaOS no-op stub for chocolate-doom's textscreen library.
 * Heretic's DOS-style startup banner needs it; the port ships no textscreen
 * backend, so these stubs satisfy the compiler/linker (TXT_Init returns 0 so
 * callers skip the path). */
#ifndef OF_TXT_MAIN_STUB_H
#define OF_TXT_MAIN_STUB_H

#define TXT_COLOR_BLINKING (1 << 3)

typedef enum
{
    TXT_COLOR_BLACK,
    TXT_COLOR_BLUE,
    TXT_COLOR_GREEN,
    TXT_COLOR_CYAN,
    TXT_COLOR_RED,
    TXT_COLOR_MAGENTA,
    TXT_COLOR_BROWN,
    TXT_COLOR_GREY,
    TXT_COLOR_DARK_GREY,
    TXT_COLOR_BRIGHT_BLUE,
    TXT_COLOR_BRIGHT_GREEN,
    TXT_COLOR_BRIGHT_CYAN,
    TXT_COLOR_BRIGHT_RED,
    TXT_COLOR_BRIGHT_MAGENTA,
    TXT_COLOR_YELLOW,
    TXT_COLOR_BRIGHT_WHITE,
} txt_color_t;

/* No textscreen backend on openfpgaOS: report init failure so callers skip
 * the graphical startup path entirely. */
static inline int  TXT_Init(void)                    { return 0; }
static inline void TXT_Shutdown(void)                { }
static inline unsigned char *TXT_GetScreenData(void) { return (unsigned char *) 0; }
static inline void TXT_UpdateScreen(void)            { }
static inline int  TXT_GetChar(void)                 { return 0; }

#endif /* OF_TXT_MAIN_STUB_H */
