/* Finite generation-aware mixer used by the shared SFX backend test. */
#ifndef TEST_AUDIO_OF_H
#define TEST_AUDIO_OF_H
#include <stdint.h>
typedef uint64_t of_mixer_handle_t;
#define OF_MIXER_HANDLE_INVALID ((of_mixer_handle_t)0)
#define OF_MIXER_GROUP_SFX 0
#define OF_MIXER_GROUP_MUSIC 1
unsigned int of_time_us(void);
void of_audio_init(void);
void of_mixer_init(int voices, int rate);
void of_mixer_pump(void);
void of_mixer_set_master_volume(int volume);
void of_mixer_set_group_volume(int group, int volume);
void of_mixer_stop_all(void);
void of_mixer_stop_h(of_mixer_handle_t voice);
void of_mixer_set_vol_lr_h(of_mixer_handle_t voice, int left, int right);
void of_mixer_set_rate_h(of_mixer_handle_t voice, int rate);
int of_mixer_handle_active(of_mixer_handle_t voice);
int of_mixer_handle_group(of_mixer_handle_t voice);
of_mixer_handle_t of_mixer_alloc_for_group_h(int group, const uint8_t *pcm,
                                          int count, int rate, int priority, int volume);
#endif
