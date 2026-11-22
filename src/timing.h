#ifndef GBOYC_TIMING_H
#define GBOYC_TIMING_H

/* DMG clock. One machine cycle (M-cycle) is 4 clock ticks ("dots"). */
#define GB_CPU_HZ		   4194304u
#define GB_DOTS_PER_MCYCLE 4u
#define GB_DOTS_PER_FRAME  70224u /* 154 lines * 456 dots */

#endif
