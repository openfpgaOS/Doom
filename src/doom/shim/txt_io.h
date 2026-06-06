/* txt_io.h — openfpgaOS stub for chocolate-doom's textscreen output.
 * No-op companions to the txt_main.h stubs; see that header for rationale. */
#ifndef OF_TXT_IO_STUB_H
#define OF_TXT_IO_STUB_H

#include "txt_main.h"

static inline void TXT_PutChar(int c)                   { (void) c; }
static inline void TXT_Puts(const char *s)              { (void) s; }
static inline void TXT_GotoXY(int x, int y)             { (void) x; (void) y; }
static inline void TXT_FGColor(txt_color_t color)       { (void) color; }
static inline void TXT_BGColor(int color, int blinking) { (void) color; (void) blinking; }

#endif /* OF_TXT_IO_STUB_H */
