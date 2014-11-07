#include <string.h>
#include <math.h>
#include "vDos.h"
#include "video.h"
#include "render.h"
#include "vga.h"
#include "pic.h"
#include "timer.h"
#include "time.h"
#include "mem.h"

#include "mouse.h"
#include "cpu.h"
#include "..\ints\int10.h"

static bool inReset = false;

static void VGA_VerticalTimer()
	{
//	if (mouse_event_type && GETFLAG(IF))
//		CPU_HW_Interrupt(0x74);
//	PIC_AddEvent(VGA_VerticalTimer, 10.0);											// Refresh screen at 50 frames per sec (so it should be 20.0, but must use 10.0!)
	PIC_AddEvent(VGA_VerticalTimer, 20.0);											// Refresh screen at 50 frames per sec (so it should be 20.0, but must use 10.0!)
	vga.draw.vertRetrace = !vga.draw.vertRetrace;									// So it's just half the times
	inReset = false;

	if (vga.mode == M_TEXT)
		{
		if (GFX_StartUpdate())
			{
			vga.draw.cursor.address = vga.config.cursor_start*2;
			newAttrChar = (Bit16u *)(MemBase+((CurMode->mode) == 7 ? 0xB0000 : 0xb8000));	// Pointer to chars+attribs
//			newAttrChar = (Bit16u *)(MemBase+((vga.gfx.miscellaneous >> 2) == 3 ? 0xb8000 : 0xb8000));	// Pointer to chars+attribs
			GFX_EndUpdate();
			}
		return;
		}
	if (!RENDER_StartUpdate())														// Check if we can actually render, else skip the rest
		return;
//	if (!(vga.crtc.mode_control&0x1))
//		vga.draw.linear_mask &= ~0x10000;
//	else
//		vga.draw.linear_mask |= 0x10000;

	Bitu drawAddress = 0;
//	for (Bitu lin = vga.draw.lines_total; lin; lin--) 
	for (Bitu lin = vga.draw.height; lin; lin--) 
		{
//		RENDER_DrawLine(&vga.fastmem[drawAddress&vga.draw.linear_mask]);
		RENDER_DrawLine(&vga.fastmem[drawAddress]);
		drawAddress += vga.config.scan_len*16;
		}
	RENDER_EndUpdate();
	}

void VGA_ForceUpdate()
	{
	if (!vga.draw.resizing)
		{
		PIC_RemoveEvents(VGA_VerticalTimer);
		VGA_VerticalTimer();
		}
	}

void VGA_ResetVertTimer(bool delay)													// Trial to sync keyboard with screen (delay: don't update, for pasting keys)
	{
	if (!vga.draw.resizing)
		{
		PIC_RemoveEvents(VGA_VerticalTimer);
		if (!delay)
			if (inReset)
				VGA_VerticalTimer();
			else
				PIC_AddEvent(VGA_VerticalTimer, 50.0);								// Refresh screen after 0.1secs
		inReset = true;
		}
	}

static void VGA_SetupDrawing()
	{
	Bitu width = vga.crtc.horizontal_display_end+1;
	Bitu height = (vga.crtc.vertical_display_end|((vga.crtc.overflow & 2)<<7)|((vga.crtc.overflow & 0x40) << 3))+1; 

	vga.draw.resizing = false;
	width *= vga.mode == M_TEXT ? 9 : 8;
	if ((width != vga.draw.width) || (height != vga.draw.height))					// Need to resize the output window?
		{
		vga.draw.width = width;
		vga.draw.height = height;
		RENDER_SetSize(width, height);
		}
	VGA_VerticalTimer();
	}

void VGA_StartResize(void)
	{
	if (!vga.draw.resizing)
		{
		PIC_RemoveEvents(VGA_VerticalTimer);
		vga.draw.resizing = true;
		PIC_AddEvent(VGA_SetupDrawing, 50.0);										// Start a resize after delay
		}
	}
