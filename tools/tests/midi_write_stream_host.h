/* Forced include for host builds of the SDK MIDI engine (test_midi_write_stream.c). */
#define OF_FASTRAM_H
#define OF_FASTDATA
#define OF_FASTRODATA
#define OF_FASTTEXT __attribute__((noinline))
#define OF_TIMER_H
#define OF_CACHE_H
#include <stdint.h>
#include "include/of_services.h"
static inline unsigned int of_time_us(void) { return (unsigned int)OF_SVC->timer_get_us(); }
static inline void of_timer_set_callback(void (*fn)(void), uint32_t hz) { OF_SVC->timer_set_callback(fn, hz); }
