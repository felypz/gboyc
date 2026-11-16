#include "apu.h"

#include <string.h>

#define CPU_HZ			   4194304U
#define FRAME_SEQ_CYCLES   8192
#define HPF_CHARGE		   0.996f
#define MIX_NORM		   (1.0f / 480.0f)
#define OUTPUT_GAIN		   1.5f
#define WAVE_TRIGGER_DELAY 6

#define ADDR_NR10 0xFF10
#define ADDR_NR50 0xFF24
#define ADDR_NR52 0xFF26
#define ADDR_WAVE 0xFF30

#define R_NR10 0x00
#define R_NR32 0x0C
#define R_NR43 0x12
#define R_NR50 0x14
#define R_NR51 0x15

static const uint8_t DUTY[4][8] = {
	{0, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 1, 1, 1},
	{0, 1, 1, 1, 1, 1, 1, 0},
};

static const uint16_t LENGTH_MAX[4] = {64, 64, 256, 64};
static const uint8_t DIVISORS[8] = {8, 16, 32, 48, 64, 80, 96, 112};

static const uint8_t READ_MASK[0x17] = {
	0x80, 0x3F, 0x00, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF,
	0x9F, 0xFF, 0xBF, 0xFF, 0xFF, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x70,
};

static const uint8_t WAVE_DEFAULT[16] = {0x84, 0x40, 0x43, 0xAA, 0x2D, 0x78, 0x92, 0x3C,
										 0x60, 0x59, 0x59, 0xB0, 0x34, 0xB8, 0x2E, 0xDA};

static const struct {
	uint16_t addr;
	uint8_t value;
} BOOT[] = {
	{0xFF26, 0x80}, {0xFF24, 0x77}, {0xFF25, 0xF3}, {0xFF10, 0x80}, {0xFF11, 0xBF}, {0xFF12, 0xF3},
	{0xFF13, 0xFF}, {0xFF14, 0x3F}, {0xFF16, 0x3F}, {0xFF17, 0x00}, {0xFF18, 0xFF}, {0xFF19, 0x3F},
	{0xFF1A, 0x7F}, {0xFF1B, 0xFF}, {0xFF1C, 0x9F}, {0xFF1D, 0xFF}, {0xFF1E, 0x3F}, {0xFF20, 0xFF},
	{0xFF21, 0x00}, {0xFF22, 0x00}, {0xFF23, 0x3F},
};

static int period(const Apu *a, unsigned i) {
	const ApuChannel *c = &a->ch[i];

	switch (i) {
	case 2:
		return (2048 - c->freq) * 2;
	case 3: {
		uint8_t n = a->reg[R_NR43];
		return DIVISORS[n & 7] << (n >> 4);
	}
	default:
		return (2048 - c->freq) * 4;
	}
}

static unsigned sweep_calc(Apu *a) {
	uint8_t nr10 = a->reg[R_NR10];
	unsigned delta = a->sweep_shadow >> (nr10 & 7);
	unsigned next;

	if (nr10 & 8) {
		next = a->sweep_shadow - delta;
		a->sweep_negated = true;
	} else {
		next = a->sweep_shadow + delta;
	}
	if (next > 2047) {
		a->ch[0].on = false;
	}
	return next;
}

static void sweep_trigger(Apu *a) {
	uint8_t nr10 = a->reg[R_NR10];
	unsigned p = (nr10 >> 4) & 7;
	unsigned s = nr10 & 7;

	a->sweep_shadow = a->ch[0].freq;
	a->sweep_timer = (uint8_t)(p ? p : 8);
	a->sweep_enabled = p || s;
	a->sweep_negated = false;
	if (s) {
		sweep_calc(a);
	}
}

static void sweep_clock(Apu *a) {
	if (--a->sweep_timer) {
		return;
	}

	uint8_t nr10 = a->reg[R_NR10];
	unsigned p = (nr10 >> 4) & 7;

	a->sweep_timer = (uint8_t)(p ? p : 8);
	if (!a->sweep_enabled || !p) {
		return;
	}

	unsigned next = sweep_calc(a);
	if (next <= 2047 && (nr10 & 7)) {
		a->sweep_shadow = (uint16_t)next;
		a->ch[0].freq = (uint16_t)next;
		sweep_calc(a);
	}
}

static void length_clock(ApuChannel *c) {
	if (c->len_enable && c->length && --c->length == 0) {
		c->on = false;
	}
}

static void envelope_clock(ApuChannel *c) {
	if (!c->env_period) {
		return;
	}
	if (c->env_timer) {
		c->env_timer--;
	}
	if (c->env_timer) {
		return;
	}

	c->env_timer = c->env_period;
	if (c->env_up && c->volume < 15) {
		c->volume++;
	} else if (!c->env_up && c->volume > 0) {
		c->volume--;
	}
}

static void frame_step(Apu *a) {
	unsigned s = a->fs_step;

	if (!(s & 1)) {
		for (unsigned i = 0; i < 4; i++) {
			length_clock(&a->ch[i]);
		}
	}
	if (s == 2 || s == 6) {
		sweep_clock(a);
	}
	if (s == 7) {
		envelope_clock(&a->ch[0]);
		envelope_clock(&a->ch[1]);
		envelope_clock(&a->ch[3]);
	}
	a->fs_step = (uint8_t)((s + 1) & 7);
}

static void lfsr_clock(Apu *a) {
	unsigned x = (a->lfsr ^ (a->lfsr >> 1)) & 1;

	a->lfsr = (uint16_t)((a->lfsr >> 1) | (x << 14));
	if (a->reg[R_NR43] & 8) {
		a->lfsr = (uint16_t)((a->lfsr & ~0x40) | (x << 6));
	}
}

static void advance(Apu *a, unsigned i) {
	ApuChannel *c = &a->ch[i];

	c->timer -= 4;
	while (c->timer <= 0) {
		c->timer += period(a, i);
		if (i == 3) {
			lfsr_clock(a);
		} else {
			c->pos = (uint8_t)((c->pos + 1) & (i == 2 ? 31 : 7));
			if (i == 2) {
				a->wave_access = true;
			}
		}
	}
}

static unsigned output(const Apu *a, unsigned i) {
	const ApuChannel *c = &a->ch[i];

	if (!c->on) {
		return 0;
	}

	switch (i) {
	case 2: {
		uint8_t b = a->wave[c->pos >> 1];
		unsigned n = (c->pos & 1) ? (b & 15u) : (b >> 4);
		unsigned code = (a->reg[R_NR32] >> 5) & 3;
		return code ? n >> (code - 1) : 0;
	}
	case 3:
		return (a->lfsr & 1) ? 0 : c->volume;
	default:
		return DUTY[c->duty][c->pos] ? c->volume : 0;
	}
}

static void mix(Apu *a) {
	uint8_t pan = a->reg[R_NR51];
	uint8_t vol = a->reg[R_NR50];
	unsigned left = 0;
	unsigned right = 0;

	for (unsigned i = 0; i < 4; i++) {
		unsigned o = output(a, i);
		if (pan & (1u << i)) {
			right += o;
		}
		if (pan & (0x10u << i)) {
			left += o;
		}
	}

	a->left_sum += left * (((unsigned)(vol >> 4) & 7) + 1);
	a->right_sum += right * (((unsigned)vol & 7) + 1);
	a->sum_count++;
}

static float filter(float in, float *cap) {
	float out = in - *cap;
	*cap = in - out * HPF_CHARGE;
	return out;
}

static float clamp(float v) {
	return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
}

static void emit(Apu *a) {
	float n = (float)a->sum_count;
	float l = filter((float)a->left_sum / n * MIX_NORM, &a->left_cap);
	float r = filter((float)a->right_sum / n * MIX_NORM, &a->right_cap);

	a->left_sum = 0;
	a->right_sum = 0;
	a->sum_count = 0;

	if (a->sample_count < APU_BUFFER_FRAMES) {
		a->samples[a->sample_count * 2] = clamp(l * OUTPUT_GAIN);
		a->samples[a->sample_count * 2 + 1] = clamp(r * OUTPUT_GAIN);
		a->sample_count++;
	}
}

static void wave_trigger_corruption(Apu *a) {
	unsigned idx = a->ch[2].pos >> 1;

	if (idx < 4) {
		a->wave[0] = a->wave[idx];
	} else {
		memcpy(a->wave, &a->wave[idx & ~3u], 4);
	}
}

static void trigger(Apu *a, unsigned i) {
	ApuChannel *c = &a->ch[i];

	if (i == 2 && c->on && a->wave_access) {
		wave_trigger_corruption(a);
	}

	c->on = c->dac;
	if (c->length == 0) {
		c->length = LENGTH_MAX[i];
		if (c->len_enable && (a->fs_step & 1)) {
			c->length--;
		}
	}
	c->timer = period(a, i) + (i == 2 ? WAVE_TRIGGER_DELAY : 0);

	if (i != 2) {
		c->volume = a->reg[i * 5 + 2] >> 4;
		c->env_timer = c->env_period ? c->env_period : 8;
	}
	if (i == 0) {
		sweep_trigger(a);
	} else if (i == 2) {
		c->pos = 0;
	} else if (i == 3) {
		a->lfsr = 0x7FFF;
	}
}

static void control_write(Apu *a, unsigned i, uint8_t value) {
	ApuChannel *c = &a->ch[i];
	bool was_enabled = c->len_enable;

	c->len_enable = value & 0x40;
	if (i != 3) {
		c->freq = (uint16_t)(((value & 7) << 8) | (c->freq & 0xFF));
	}

	if (!was_enabled && c->len_enable && (a->fs_step & 1) && c->length) {
		if (--c->length == 0 && !(value & 0x80)) {
			c->on = false;
		}
	}
	if (value & 0x80) {
		trigger(a, i);
	}
}

static void power_off(Apu *a) {
	uint16_t length[4];

	for (unsigned i = 0; i < 4; i++) {
		length[i] = a->ch[i].length;
	}

	memset(a->reg, 0, sizeof a->reg);
	memset(a->ch, 0, sizeof a->ch);

	for (unsigned i = 0; i < 4; i++) {
		a->ch[i].length = length[i];
	}

	a->power = false;
	a->sweep_timer = 8;
	a->sweep_enabled = false;
	a->sweep_negated = false;
}

static void power_on(Apu *a) {
	a->power = true;
	a->fs_step = 0;
	a->fs_cycles = 0;
}

static void envelope_write(ApuChannel *c, uint8_t value) {
	bool up = value & 8;

	if (c->on) {
		if (!c->env_period) {
			c->volume++;
		} else if (!c->env_up) {
			c->volume += 2;
		}
		if (up != c->env_up) {
			c->volume = (uint8_t)(16 - c->volume);
		}
		c->volume &= 15;
	}

	c->env_period = value & 7;
	c->env_up = up;
	c->dac = (value & 0xF8) != 0;
	if (!c->dac) {
		c->on = false;
	}
}

void apu_reset(Apu *a, bool boot) {
	memset(a, 0, sizeof *a);
	memcpy(a->wave, WAVE_DEFAULT, sizeof a->wave);
	a->sweep_timer = 8;
	a->lfsr = 0x7FFF;
	if (boot) {
		return;
	}

	for (size_t i = 0; i < sizeof BOOT / sizeof *BOOT; i++) {
		apu_write(a, BOOT[i].addr, BOOT[i].value);
	}

	a->ch[0].on = true;
	a->ch[0].volume = 0;
}

void apu_step(Apu *a, int cycles) {
	for (int n = 0; n < cycles; n += 4) {
		a->wave_access = false;

		if (a->power) {
			a->fs_cycles += 4;
			if (a->fs_cycles >= FRAME_SEQ_CYCLES) {
				a->fs_cycles -= FRAME_SEQ_CYCLES;
				frame_step(a);
			}
			for (unsigned i = 0; i < 4; i++) {
				if (a->ch[i].on) {
					advance(a, i);
				}
			}
		}

		mix(a);
		a->sample_acc += 4 * APU_SAMPLE_RATE;
		if (a->sample_acc >= CPU_HZ) {
			a->sample_acc -= CPU_HZ;
			emit(a);
		}
	}
}

uint8_t apu_read(const Apu *a, uint16_t addr) {
	if (addr >= ADDR_WAVE) {
		if (a->ch[2].on) {
			return a->wave_access ? a->wave[a->ch[2].pos >> 1] : 0xFF;
		}
		return a->wave[addr - ADDR_WAVE];
	}
	if (addr > ADDR_NR52) {
		return 0xFF;
	}
	if (addr == ADDR_NR52) {
		uint8_t status = a->power ? 0x80 : 0;
		for (unsigned i = 0; i < 4; i++) {
			status |= a->ch[i].on ? (1u << i) : 0;
		}
		return 0x70 | status;
	}

	unsigned r = addr - ADDR_NR10;
	return a->reg[r] | READ_MASK[r];
}

void apu_write(Apu *a, uint16_t addr, uint8_t value) {
	if (addr >= ADDR_WAVE) {
		if (!a->ch[2].on) {
			a->wave[addr - ADDR_WAVE] = value;
		} else if (a->wave_access) {
			a->wave[a->ch[2].pos >> 1] = value;
		}
		return;
	}
	if (addr == ADDR_NR52) {
		if (!(value & 0x80) && a->power) {
			power_off(a);
		} else if ((value & 0x80) && !a->power) {
			power_on(a);
		}
		return;
	}
	if (addr < ADDR_NR10 || addr > 0xFF25) {
		return;
	}

	if (!a->power) {
		unsigned off = addr - ADDR_NR10;

		if (off < 20 && off % 5 == 1) {
			unsigned i = off / 5;
			uint8_t mask = (i == 2) ? 0xFF : 0x3F;

			a->ch[i].length = (uint16_t)(LENGTH_MAX[i] - (value & mask));
		}
		return;
	}

	a->reg[addr - ADDR_NR10] = value;
	if (addr >= ADDR_NR50) {
		return;
	}

	unsigned i = (addr - ADDR_NR10) / 5;
	unsigned r = (addr - ADDR_NR10) % 5;
	ApuChannel *c = &a->ch[i];

	switch (r) {
	case 0:
		if (i == 0 && !(value & 8) && a->sweep_negated) {
			c->on = false;
		} else if (i == 2) {
			c->dac = (value & 0x80) != 0;
			if (!c->dac) {
				c->on = false;
			}
		}
		break;
	case 1:
		if (i < 2) {
			c->duty = value >> 6;
		}
		c->length = (uint16_t)(LENGTH_MAX[i] - (i == 2 ? value : (value & 0x3F)));
		break;
	case 2:
		if (i != 2) {
			envelope_write(c, value);
		}
		break;
	case 3:
		if (i != 3) {
			c->freq = (uint16_t)((c->freq & 0x700) | value);
		}
		break;
	default:
		control_write(a, i, value);
		break;
	}
}

size_t apu_take_samples(Apu *a, const float **out) {
	size_t n = a->sample_count;

	*out = a->samples;
	a->sample_count = 0;
	return n;
}
