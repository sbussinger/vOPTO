#include "vDos.h"
#include "video.h"
#include "pic.h"
#include "vga.h"

VGA_Type vga;

Bit32u ExpandTable[256];
Bit32u Expand16Table[4][16];
Bit32u FillTable[16];

void VGA_SetMode(VGAModes mode)
	{
	if (vga.mode != mode)
		{
		vga.mode = mode;
		VGA_SetupHandlers();
		VGA_StartResize();
		}
	}

void VGA_DetermineMode(void)
	{
	if (vga.attr.mode_control&1)													// Test for VGA output active or direct color modes
		VGA_SetMode(M_EGA);
	else
		VGA_SetMode(M_TEXT);
	}

void VGA_Init()
	{
	vga.draw.resizing = false;
	vga.mode = M_ERROR;																// For first init
	vga.vmemsize = 256*1024;														// 256kB VGA memory
	VGA_SetupMemory();																// Memory is allocated here
	VGA_SetupMisc();
	VGA_SetupDAC();
	VGA_SetupGFX();
	VGA_SetupSEQ();
	VGA_SetupAttr();

	for (Bitu i = 0; i < 256; i++)													// Generate tables
		ExpandTable[i] = i | (i << 8) | (i <<16) | (i << 24);
	for (Bitu i = 0; i < 16; i++)
		FillTable[i] = ((i&1) ? 0x000000ff : 0)|((i&2) ? 0x0000ff00:0)|((i&4) ? 0x00ff0000 : 0)|((i&8) ? 0xff000000 : 0);
	for (Bitu j = 0; j < 4; j++)
		for (Bitu i = 0; i < 16; i++)
			Expand16Table[j][i] = ((i&1) ? 1<<(24+j) : 0)|((i&2) ? 1<<(16+j) : 0)|((i&4) ? 1<<(8+j) : 0)|((i&8) ? 1<<j : 0);
	}
