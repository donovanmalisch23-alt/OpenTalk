/* SPDX-License-Identifier: GPL-3.0-only (this file); miniaudio itself: Unlicense OR MIT-0 */
/* The one translation unit that compiles miniaudio's implementation. Only the device
   I/O, decoding and resampling parts are used; the high-level engine is left out. */
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_GENERATION
#include "miniaudio.h"
