#include "apu.h"

#include <string.h>

#include "memmap.h"
#include "timing.h"

#define FRAME_SEQ_CYCLES   8192 /* 512 Hz frame sequencer */
#define HPF_CHARGE		   0.996f
#define MIX_NORM		   (1.0f / 480.0f)
#define OUTPUT_GAIN		   1.5f
#define WAVE_TRIGGER_DELAY 6
#define WAVE_READ_OFFSET   2 /* timer value at which the wave RAM byte is readable while on */
#define STEP_DOTS		   ((int)GB_DOTS_PER_MCYCLE)

/*
 * Sanity bounds for a channel timer, used to validate loaded savestates.
 * The timer is decremented by one M-cycle and then reloaded while <= 0, so it
 * can only dip a few dots below zero. The upper bound exceeds the longest
 * period (noise: divisor 112 << 15 = 3670016).
 */
#define APU_TIMER_MIN (-16)
#define APU_TIMER_MAX 4200000

#define REG_NR32 0xFF1C
#define REG_NR43 0xFF22
#define REG_NR50 0xFF24
#define REG_NR51 0xFF25

#define SWEEP_MAX_FREQ		2047
#define SWEEP_DEFAULT_TIMER 8
#define PAN_LEFT_SHIFT		4
#define WAVE_SAMPLES		32
#define PULSE_STEPS			8

/* Index into Apu.reg for a register address. */
#define REG_INDEX(addr) ((addr) - REG_NR10)

static const uint8_t DUTY[4][PULSE_STEPS] = {
	{0, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 1, 1, 1},
	{0, 1, 1, 1, 1, 1, 1, 0},
};

static const uint16_t LENGTH_MAX[CH_COUNT] = {64, 64, 256, 64};
static const uint8_t NOISE_DIVISORS[8] = {8, 16, 32, 48, 64, 80, 96, 112};

/* Bits that always read back as 1, per register (NR10..NR52). */
static const uint8_t READ_MASK[APU_REG_COUNT] = {
	0x80, 0x3F, 0x00, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF,
	0x9F, 0xFF, 0xBF, 0xFF, 0xFF, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x70,
};

static const uint8_t WAVE_DEFAULT[16] = {0x84, 0x40, 0x43, 0xAA, 0x2D, 0x78, 0x92, 0x3C,
										 0x60, 0x59, 0x59, 0xB0, 0x34, 0xB8, 0x2E, 0xDA};

/* Register values left behind by the boot ROM (applied only when it is skipped). */
static const struct {
	uint16_t addr;
	uint8_t value;
} POST_BOOT_REGS[] = {
	{0xFF26, 0x80}, {0xFF24, 0x77}, {0xFF25, 0xF3}, {0xFF10, 0x80}, {0xFF11, 0xBF}, {0xFF12, 0xF3},
	{0xFF13, 0xFF}, {0xFF14, 0x3F}, {0xFF16, 0x3F}, {0xFF17, 0x00}, {0xFF18, 0xFF}, {0xFF19, 0x3F},
	{0xFF1A, 0x7F}, {0xFF1B, 0xFF}, {0xFF1C, 0x9F}, {0xFF1D, 0xFF}, {0xFF1E, 0x3F}, {0xFF20, 0xFF},
	{0xFF21, 0x00}, {0xFF22, 0x00}, {0xFF23, 0x3F},
};

static bool is_pulse(ApuChannelId ch) {
	return ch == CH_PULSE1 || ch == CH_PULSE2;
}

/* NRx2 (envelope / wave volume) of a channel: registers are 5 apart. */
static uint8_t nrx2(const Apu *a, ApuChannelId ch) {
	return a->reg[(unsigned)ch * 5 + 2];
}

static int period(const Apu *a, ApuChannelId ch) {
	const ApuChannel *c = &a->ch[ch];

	switch (ch) {
	case CH_WAVE:
		return (2048 - c->freq) * 2;
	case CH_NOISE: {
		uint8_t nr43 = a->reg[REG_INDEX(REG_NR43)];
		return NOISE_DIVISORS[nr43 & 7] << (nr43 >> 4);
	}
	default:
		return (2048 - c->freq) * 4;
	}
}

static unsigned sweep_period(uint8_t nr10) {
	unsigned p = (nr10 >> 4) & 7;

	return p ? p : SWEEP_DEFAULT_TIMER;
}

/* Computes the next sweep frequency; overflow silences channel 1. */
static unsigned sweep_next_freq(Apu *a) {
	uint8_t nr10 = a->reg[REG_INDEX(REG_NR10)];
	unsigned delta = a->sweep_shadow >> (nr10 & 7);
	unsigned next;

	if (nr10 & 8) {
		next = a->sweep_shadow - delta;
		a->sweep_negated = true;
	} else {
		next = a->sweep_shadow + delta;
	}
	if (next > SWEEP_MAX_FREQ) {
		a->ch[CH_PULSE1].on = false;
	}
	return next;
}

static void sweep_trigger(Apu *a) {
	uint8_t nr10 = a->reg[REG_INDEX(REG_NR10)];
	unsigned pace = (nr10 >> 4) & 7;
	unsigned shift = nr10 & 7;

	a->sweep_shadow = a->ch[CH_PULSE1].freq;
	a->sweep_timer = (uint8_t)sweep_period(nr10);
	a->sweep_enabled = pace || shift;
	a->sweep_negated = false;
	if (shift) {
		sweep_next_freq(a);
	}
}

static void sweep_clock(Apu *a) {
	if (--a->sweep_timer) {
		return;
	}

	uint8_t nr10 = a->reg[REG_INDEX(REG_NR10)];
	unsigned pace = (nr10 >> 4) & 7;

	a->sweep_timer = (uint8_t)sweep_period(nr10);
	if (!a->sweep_enabled || !pace) {
		return;
	}

	unsigned next = sweep_next_freq(a);
	if (next <= SWEEP_MAX_FREQ && (nr10 & 7)) {
		a->sweep_shadow = (uint16_t)next;
		a->ch[CH_PULSE1].freq = (uint16_t)next;
		sweep_next_freq(a); /* second overflow check, result unused */
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

/* Steps 0,2,4,6 clock length; 2 and 6 sweep; 7 envelope. */
static void frame_step(Apu *a) {
	unsigned s = a->fs_step;

	if (!(s & 1)) {
		for (ApuChannelId i = 0; i < CH_COUNT; i++) {
			length_clock(&a->ch[i]);
		}
	}
	if (s == 2 || s == 6) {
		sweep_clock(a);
	}
	if (s == 7) {
		envelope_clock(&a->ch[CH_PULSE1]);
		envelope_clock(&a->ch[CH_PULSE2]);
		envelope_clock(&a->ch[CH_NOISE]);
	}
	a->fs_step = (uint8_t)((s + 1) & 7);
}

static void lfsr_clock(Apu *a) {
	unsigned x = (a->lfsr ^ (a->lfsr >> 1)) & 1;

	a->lfsr = (uint16_t)((a->lfsr >> 1) | (x << 14));
	if (a->reg[REG_INDEX(REG_NR43)] & 8) { /* 7-bit width mode */
		a->lfsr = (uint16_t)((a->lfsr & ~0x40) | (x << 6));
	}
}

static void advance(Apu *a, ApuChannelId ch) {
	ApuChannel *c = &a->ch[ch];

	c->timer -= STEP_DOTS;
	while (c->timer <= 0) {
		c->timer += period(a, ch);
		if (ch == CH_NOISE) {
			lfsr_clock(a);
		} else {
			c->pos = (uint8_t)((c->pos + 1) & (ch == CH_WAVE ? WAVE_SAMPLES - 1 : PULSE_STEPS - 1));
		}
	}
}

static unsigned output(const Apu *a, ApuChannelId ch) {
	const ApuChannel *c = &a->ch[ch];

	if (!c->on) {
		return 0;
	}

	switch (ch) {
	case CH_WAVE: {
		uint8_t byte = a->wave[c->pos >> 1];
		unsigned sample = (c->pos & 1) ? (byte & 15u) : (byte >> 4);
		unsigned volume_code = (a->reg[REG_INDEX(REG_NR32)] >> 5) & 3;
		return volume_code ? sample >> (volume_code - 1) : 0;
	}
	case CH_NOISE:
		return (a->lfsr & 1) ? 0 : c->volume;
	default:
		return DUTY[c->duty][c->pos] ? c->volume : 0;
	}
}

static void mix(Apu *a) {
	uint8_t pan = a->reg[REG_INDEX(REG_NR51)];
	uint8_t vol = a->reg[REG_INDEX(REG_NR50)];
	unsigned left = 0;
	unsigned right = 0;

	for (ApuChannelId i = 0; i < CH_COUNT; i++) {
		unsigned level = output(a, i);

		if (pan & (1u << i)) {
			right += level;
		}
		if (pan & (1u << (i + PAN_LEFT_SHIFT))) {
			left += level;
		}
	}

	a->left_sum += left * (((unsigned)(vol >> 4) & 7) + 1);
	a->right_sum += right * (((unsigned)vol & 7) + 1);
	a->sum_count++;
}

/* One-pole DC-blocking filter, like the DMG output capacitor. */
static float high_pass(float in, float *cap) {
	float out = in - *cap;

	*cap = in - out * HPF_CHARGE;
	return out;
}

static float clamp(float v) {
	return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
}

static void emit(Apu *a) {
	float n = (float)a->sum_count;
	float l = high_pass((float)a->left_sum / n * MIX_NORM, &a->left_cap);
	float r = high_pass((float)a->right_sum / n * MIX_NORM, &a->right_cap);

	a->left_sum = 0;
	a->right_sum = 0;
	a->sum_count = 0;

	if (a->sample_count < APU_BUFFER_FRAMES) {
		a->samples[a->sample_count * 2] = clamp(l * OUTPUT_GAIN);
		a->samples[a->sample_count * 2 + 1] = clamp(r * OUTPUT_GAIN);
		a->sample_count++;
	}
}

/*
 * While the wave channel plays, the CPU can only reach the wave RAM byte the
 * channel is currently fetching, and only on the cycle it does so.
 * Returns that byte's index, or -1 when no access is possible.
 */
static int wave_hit(const Apu *a) {
	const ApuChannel *c = &a->ch[CH_WAVE];

	if (!c->on || c->timer != WAVE_READ_OFFSET) {
		return -1;
	}
	return ((c->pos + 1) & (WAVE_SAMPLES - 1)) >> 1;
}

/* Retriggering the wave channel while it reads RAM corrupts the first bytes (DMG quirk). */
static void wave_trigger_corruption(Apu *a, unsigned idx) {
	if (idx < 4) {
		a->wave[0] = a->wave[idx];
	} else {
		memcpy(a->wave, &a->wave[idx & ~3u], 4);
	}
}

static void trigger(Apu *a, ApuChannelId ch) {
	ApuChannel *c = &a->ch[ch];

	if (ch == CH_WAVE && c->on) {
		int idx = wave_hit(a);

		if (idx >= 0) {
			wave_trigger_corruption(a, (unsigned)idx);
		}
	}

	c->on = c->dac;
	if (c->length == 0) {
		c->length = LENGTH_MAX[ch];
		if (c->len_enable && (a->fs_step & 1)) {
			c->length--;
		}
	}
	c->timer = period(a, ch) + (ch == CH_WAVE ? WAVE_TRIGGER_DELAY : 0);

	if (ch != CH_WAVE) {
		c->volume = nrx2(a, ch) >> 4;
		c->env_timer = c->env_period ? c->env_period : 8;
	}
	if (ch == CH_PULSE1) {
		sweep_trigger(a);
	} else if (ch == CH_WAVE) {
		c->pos = 0;
	} else if (ch == CH_NOISE) {
		a->lfsr = 0x7FFF;
	}
}

/* NRx1 length load. The wave channel has a full 8-bit length, the others 6 bits. */
static void length_load(Apu *a, ApuChannelId ch, uint8_t value) {
	uint8_t mask = (ch == CH_WAVE) ? 0xFF : 0x3F;

	a->ch[ch].length = (uint16_t)(LENGTH_MAX[ch] - (value & mask));
}

/* NRx4: length enable (bit 6), trigger (bit 7), frequency high bits. */
static void control_write(Apu *a, ApuChannelId ch, uint8_t value) {
	ApuChannel *c = &a->ch[ch];
	bool was_enabled = c->len_enable;

	c->len_enable = value & 0x40;
	if (ch != CH_NOISE) {
		c->freq = (uint16_t)(((value & 7) << 8) | (c->freq & 0xFF));
	}

	/* Enabling length on an odd frame-sequencer step clocks it immediately. */
	if (!was_enabled && c->len_enable && (a->fs_step & 1) && c->length) {
		if (--c->length == 0 && !(value & 0x80)) {
			c->on = false;
		}
	}
	if (value & 0x80) {
		trigger(a, ch);
	}
}

/* Powering off clears every register but keeps the length counters (DMG behavior). */
static void power_off(Apu *a) {
	uint16_t length[CH_COUNT];

	for (ApuChannelId i = 0; i < CH_COUNT; i++) {
		length[i] = a->ch[i].length;
	}

	memset(a->reg, 0, sizeof a->reg);
	memset(a->ch, 0, sizeof a->ch);

	for (ApuChannelId i = 0; i < CH_COUNT; i++) {
		a->ch[i].length = length[i];
	}

	a->power = false;
	a->sweep_timer = SWEEP_DEFAULT_TIMER;
	a->sweep_enabled = false;
	a->sweep_negated = false;
}

static void power_on(Apu *a) {
	a->power = true;
	a->fs_step = 0;
	a->fs_cycles = 0;
}

/* NRx2 write, including the DMG "zombie mode" volume glitch while the channel is on. */
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

void apu_reset(Apu *a, bool has_boot_rom) {
	memset(a, 0, sizeof *a);
	memcpy(a->wave, WAVE_DEFAULT, sizeof a->wave);
	a->sweep_timer = SWEEP_DEFAULT_TIMER;
	a->lfsr = 0x7FFF;
	if (has_boot_rom) {
		return; /* the boot ROM programs the registers itself */
	}

	for (size_t i = 0; i < sizeof POST_BOOT_REGS / sizeof *POST_BOOT_REGS; i++) {
		apu_write(a, POST_BOOT_REGS[i].addr, POST_BOOT_REGS[i].value);
	}

	a->ch[CH_PULSE1].on = true;
	a->ch[CH_PULSE1].volume = 0;
}

void apu_step(Apu *a, int cycles) {
	for (int n = 0; n < cycles; n += STEP_DOTS) {
		if (a->power) {
			a->fs_cycles += STEP_DOTS;
			if (a->fs_cycles >= FRAME_SEQ_CYCLES) {
				a->fs_cycles -= FRAME_SEQ_CYCLES;
				frame_step(a);
			}
			for (ApuChannelId i = 0; i < CH_COUNT; i++) {
				if (a->ch[i].on) {
					advance(a, i);
				}
			}
		}

		mix(a);
		a->sample_acc += GB_DOTS_PER_MCYCLE * APU_SAMPLE_RATE;
		if (a->sample_acc >= GB_CPU_HZ) {
			a->sample_acc -= GB_CPU_HZ;
			emit(a);
		}
	}
}

uint8_t apu_read(const Apu *a, uint16_t addr) {
	if (addr >= REG_WAVE_START) {
		if (a->ch[CH_WAVE].on) {
			int idx = wave_hit(a);
			return idx >= 0 ? a->wave[idx] : 0xFF;
		}
		return a->wave[addr - REG_WAVE_START];
	}
	if (addr > REG_NR52) {
		return 0xFF; /* unused registers between NR52 and wave RAM */
	}
	if (addr == REG_NR52) {
		uint8_t status = a->power ? 0x80 : 0;

		for (ApuChannelId i = 0; i < CH_COUNT; i++) {
			status |= a->ch[i].on ? (1u << i) : 0;
		}
		return 0x70 | status;
	}

	unsigned r = REG_INDEX(addr);
	return a->reg[r] | READ_MASK[r];
}

void apu_write(Apu *a, uint16_t addr, uint8_t value) {
	if (addr >= REG_WAVE_START) {
		if (!a->ch[CH_WAVE].on) {
			a->wave[addr - REG_WAVE_START] = value;
		} else {
			int idx = wave_hit(a);
			if (idx >= 0) {
				a->wave[idx] = value;
			}
		}
		return;
	}
	if (addr == REG_NR52) {
		if (!(value & 0x80) && a->power) {
			power_off(a);
		} else if ((value & 0x80) && !a->power) {
			power_on(a);
		}
		return;
	}
	if (addr < REG_NR10 || addr > REG_NR51) {
		return;
	}

	unsigned off = REG_INDEX(addr);

	if (!a->power) {
		/* Only the length counters (NRx1) stay writable while powered off. */
		if (off < CH_COUNT * 5 && off % 5 == 1) {
			length_load(a, (ApuChannelId)(off / 5), value);
		}
		return;
	}

	a->reg[off] = value;
	if (addr >= REG_NR50) {
		return; /* master volume / panning: stored only */
	}

	ApuChannelId ch = (ApuChannelId)(off / 5);
	unsigned r = off % 5;
	ApuChannel *c = &a->ch[ch];

	switch (r) {
	case 0: /* NRx0: sweep (pulse 1) or wave DAC enable */
		if (ch == CH_PULSE1 && !(value & 8) && a->sweep_negated) {
			c->on = false;
		} else if (ch == CH_WAVE) {
			c->dac = (value & 0x80) != 0;
			if (!c->dac) {
				c->on = false;
			}
		}
		break;
	case 1: /* NRx1: duty (pulse) and length */
		if (is_pulse(ch)) {
			c->duty = value >> 6;
		}
		length_load(a, ch, value);
		break;
	case 2: /* NRx2: envelope (wave uses it as a plain volume register) */
		if (ch != CH_WAVE) {
			envelope_write(c, value);
		}
		break;
	case 3: /* NRx3: frequency low byte */
		if (ch != CH_NOISE) {
			c->freq = (uint16_t)((c->freq & 0x700) | value);
		}
		break;
	default:
		control_write(a, ch, value);
		break;
	}
}

size_t apu_take_samples(Apu *a, const float **out) {
	size_t n = a->sample_count;

	*out = a->samples;
	a->sample_count = 0;
	return n;
}

bool apu_state_sane(const Apu *a) {
	if (a->fs_step >= 8 || a->sample_count > APU_BUFFER_FRAMES) {
		return false;
	}
	for (ApuChannelId i = 0; i < CH_COUNT; i++) {
		const ApuChannel *c = &a->ch[i];
		unsigned max_pos = (i == CH_WAVE) ? WAVE_SAMPLES : PULSE_STEPS;

		if (c->duty > 3 || c->pos >= max_pos || c->freq > 2047 || c->length > 256 ||
			c->timer < APU_TIMER_MIN || c->timer > APU_TIMER_MAX) {
			return false;
		}
	}
	return true;
}
