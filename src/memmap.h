#ifndef GBOYC_MEMMAP_H
#define GBOYC_MEMMAP_H

/* Game Boy (DMG) address map. Every bound is inclusive. */

#define MEM_ROM_START	   0x0000
#define MEM_ROM_END		   0x7FFF
#define MEM_VRAM_START	   0x8000
#define MEM_VRAM_END	   0x9FFF
#define MEM_EXT_RAM_START  0xA000
#define MEM_EXT_RAM_END	   0xBFFF
#define MEM_WRAM_START	   0xC000
#define MEM_WRAM_END	   0xDFFF
#define MEM_ECHO_START	   0xE000
#define MEM_ECHO_END	   0xFDFF
#define MEM_OAM_START	   0xFE00
#define MEM_OAM_END		   0xFE9F
#define MEM_UNUSABLE_START 0xFEA0
#define MEM_UNUSABLE_END   0xFEFF
#define MEM_IO_START	   0xFF00
#define MEM_IO_END		   0xFF7F
#define MEM_HRAM_START	   0xFF80
#define MEM_HRAM_END	   0xFFFE

#define MEM_VRAM_SIZE (MEM_VRAM_END - MEM_VRAM_START + 1)
#define MEM_WRAM_SIZE (MEM_WRAM_END - MEM_WRAM_START + 1)
#define MEM_OAM_SIZE  (MEM_OAM_END - MEM_OAM_START + 1)
#define MEM_IO_SIZE	  (MEM_IO_END - MEM_IO_START + 1)
#define MEM_HRAM_SIZE (MEM_HRAM_END - MEM_HRAM_START + 1)

/* I/O registers */
#define REG_JOYP 0xFF00
#define REG_SB	 0xFF01
#define REG_SC	 0xFF02
#define REG_DIV	 0xFF04
#define REG_TIMA 0xFF05
#define REG_TMA	 0xFF06
#define REG_TAC	 0xFF07
#define REG_IF	 0xFF0F

#define REG_NR10	   0xFF10 /* first APU register */
#define REG_NR52	   0xFF26 /* APU power / channel status */
#define REG_WAVE_START 0xFF30
#define REG_WAVE_END   0xFF3F

#define REG_LCDC 0xFF40
#define REG_STAT 0xFF41
#define REG_SCY	 0xFF42
#define REG_SCX	 0xFF43
#define REG_LY	 0xFF44
#define REG_LYC	 0xFF45
#define REG_DMA	 0xFF46
#define REG_BGP	 0xFF47
#define REG_OBP0 0xFF48
#define REG_OBP1 0xFF49
#define REG_WY	 0xFF4A
#define REG_WX	 0xFF4B

#define REG_BOOT 0xFF50 /* any non-zero write unmaps the boot ROM */
#define REG_IE	 0xFFFF

/* Interrupt flag/enable registers only use the low 5 bits. */
#define IRQ_MASK 0x1F

#endif
