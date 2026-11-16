#ifndef GBOYC_BUS_H
#define GBOYC_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cartridge.h"

#define IRQ_VBLANK 0
#define IRQ_STAT   1
#define IRQ_TIMER  2
#define IRQ_SERIAL 3
#define IRQ_JOYPAD 4

void bus_init(Cartridge *cart);
uint8_t bus_read(uint16_t addr);
void bus_write(uint16_t addr, uint8_t value);

void bus_tick(int cycles);
void bus_request_irq(unsigned irq);
void bus_set_joypad(uint8_t mask);

bool bus_frame_ready(void);
const uint32_t *bus_framebuffer(void);
size_t bus_take_audio(const float **samples);

#endif
