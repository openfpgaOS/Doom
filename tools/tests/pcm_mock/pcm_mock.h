#ifndef PCM_MOCK_H
#define PCM_MOCK_H
#include <stdint.h>
typedef uint64_t of_mixer_handle_t;
#define OF_MIXER_HANDLE_INVALID ((of_mixer_handle_t)0)
#define OF_MIXER_GROUP_MUSIC 1
of_mixer_handle_t of_mixer_alloc_for_group_h(int, const uint8_t *, int, int, int, int);
void of_mixer_stop_h(of_mixer_handle_t);
void of_mixer_set_loop_h(of_mixer_handle_t, int, int);
void of_mixer_set_vol_lr_h(of_mixer_handle_t, int, int);
void of_mixer_set_group_volume(int, int);
void of_mixer_set_rate_h(of_mixer_handle_t, int);
int of_mixer_get_position_h(of_mixer_handle_t);
void of_cache_flush_range(const void *, uint32_t);
void of_cache_inval_range(const void *, uint32_t);
void *of_uncached(void *);
int of_file_read_async(int, uint32_t, void *, uint32_t, void (*)(int, int));
int of_file_async_busy(void);
int of_file_async_poll(void);
int of_file_slot_find(const char *, uint32_t *);
uint32_t of_file_async_max_read(void);
uint32_t of_time_ms(void);
uint32_t of_time_us(void);
typedef struct { uint32_t present_count; } of_video_timing_t;
void of_video_get_timing(of_video_timing_t *);
#endif
