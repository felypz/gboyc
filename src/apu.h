#ifndef GBOYC_APU_H
#define GBOYC_APU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APU_SAMPLE_RATE	  48000
#define APU_BUFFER_FRAMES 4096

/* Registers NR10..NR51 are stored in Apu.reg, indexed from REG_NR10. */
#define APU_REG_COUNT 0x17

typedef enum {
	CH_PULSE1,
	CH_PULSE2,
	CH_WAVE,
	CH_NOISE,
	CH_COUNT,
} ApuChannelId;

typedef struct {
	bool on;
	bool dac;
	bool len_enable;
	bool env_up;
	uint8_t volume;
	uint8_t env_period;
	uint8_t env_timer;
	uint8_t duty;
	uint8_t pos;
	uint16_t length;
	uint16_t freq;
	int timer;
} ApuChannel;

typedef struct {
	ApuChannel ch[CH_COUNT];
	uint8_t reg[APU_REG_COUNT];
	uint8_t wave[16];
	bool power;
	uint8_t fs_step;
	uint16_t fs_cycles;
	uint8_t sweep_timer;
	bool sweep_enabled;
	bool sweep_negated;
	uint16_t sweep_shadow;
	uint16_t lfsr;
	uint32_t left_sum;
	uint32_t right_sum;
	uint32_t sum_count;
	uint32_t sample_acc;
	float left_cap;
	float right_cap;
	size_t sample_count;
	float samples[APU_BUFFER_FRAMES * 2];
} Apu;

void apu_reset(Apu *a, bool has_boot_rom);
void apu_step(Apu *a, int cycles);
uint8_t apu_read(const Apu *a, uint16_t addr);
void apu_write(Apu *a, uint16_t addr, uint8_t value);
size_t apu_take_samples(Apu *a, const float **out);
/* Range checks used to validate a loaded savestate. */
bool apu_state_sane(const Apu *a);

#endif
