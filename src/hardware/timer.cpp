#include <math.h>
#include "vDos.h"
#include "inout.h"
#include "pic.h"
#include "timer.h"

static inline void BIN2BCD(Bit16u& val)
	{
	Bit16u temp = val%10+(((val/10)%10)<<4)+(((val/100)%10)<<8)+(((val/1000)%10)<<12);
	val = temp;
	}

static inline void BCD2BIN(Bit16u& val)
	{
	Bit16u temp = (val&0x0f)+((val>>4)&0x0f)*10+((val>>8)&0x0f)*100+((val>>12)&0x0f) *1000;
	val = temp;
	}

static struct PIT_Block {
	Bitu cntr;
	float delay;
	double start;

	Bit16u read_latch;
	Bit16u write_latch;

	Bit8u mode;
	Bit8u read_state;
	Bit8u write_state;

	bool bcd;
	bool go_read_latch;
	bool new_mode;
	bool update_count;
}pit0;

static void PIT0_Event()
	{
	PIC_ActivateIRQ(0);
	if (pit0.mode != 0)
		{
		pit0.start += pit0.delay;
		if (pit0.update_count)
			{
			pit0.delay = (1000.0f/((float)PIT_TICK_RATE/(float)pit0.cntr));
			pit0.update_count = false;
			}
		PIC_AddEvent(PIT0_Event, pit0.delay);
		}
	}

static bool counter_output()
	{
	double index = PIC_FullIndex()-pit0.start;
	switch (pit0.mode)
		{
	case 0:
		if (pit0.new_mode)
			return false;
		if (index > pit0.delay)
			return true;
		else
			return false;
		break;
	case 2:
		if (pit0.new_mode)
			return true;
		index = fmod(index, (double)pit0.delay);
		return index > 0;
	case 3:
		if (pit0.new_mode)
			return true;
		index = fmod(index, (double)pit0.delay);
		return index*2 < pit0.delay;
	case 4:
		// Only low on terminal count
		// if(fmod(index,(double)p->delay) == 0) return false; //Maybe take one rate tick in consideration
		// Easiest solution is to report always high (Space marines uses this mode)
		return true;
	default:
		return true;
		}
	}

static void counter_latch()
	{
	// Fill the read_latch of the selected counter with current count
	pit0.go_read_latch = false;

	if (pit0.new_mode)
		{
		double passed_time = PIC_FullIndex() - pit0.start;
		Bitu ticks_since_then = (Bitu)(passed_time / (1000.0/PIT_TICK_RATE));
		pit0.read_latch -= ticks_since_then;
		return;
		}
	double index = PIC_FullIndex()-pit0.start;
	switch (pit0.mode)
		{
	case 4:																			// Software Triggered Strobe
	case 0:																			// Interrupt on Terminal Count
		// Counter keeps on counting after passing terminal count
		if (index > pit0.delay)
			{
			index -= pit0.delay;
			if (pit0.bcd)
				{
				index = fmod(index, (1000.0/PIT_TICK_RATE)*10000.0);
				pit0.read_latch = (Bit16u)(9999-index*(PIT_TICK_RATE/1000.0));
				}
			else
				{
				index = fmod(index, (1000.0/PIT_TICK_RATE)*(double)0x10000);
				pit0.read_latch = (Bit16u)(0xffff-index*(PIT_TICK_RATE/1000.0));
				}
			}
		else
			pit0.read_latch = (Bit16u)(pit0.cntr-index*(PIT_TICK_RATE/1000.0));
		break;
	case 1:		// countdown
/*		if (p->counting)
			{
			if (index>p->delay)				// has timed out
				p->read_latch = 0xffff;		//unconfirmed
			else
				p->read_latch = (Bit16u)(p->cntr-index*(PIT_TICK_RATE/1000.0));
			}
*/		break;
	case 2:																			// Rate Generator
		index = fmod(index, (double)pit0.delay);
		pit0.read_latch = (Bit16u)(pit0.cntr - (index/pit0.delay)*pit0.cntr);
		break;
	case 3:																			// Square Wave Rate Generator
		index = fmod(index, (double)pit0.delay);
		index *= 2;
		if (index > pit0.delay)
			index -= pit0.delay;
		pit0.read_latch = (Bit16u)(pit0.cntr - (index/pit0.delay)*pit0.cntr);
		// In mode 3 it never returns odd numbers LSB (if odd number is written 1 will be
		// subtracted on first clock and then always 2)
		// fixes "Corncob 3D"
		pit0.read_latch &= 0xfffe;
		break;
	default:
		pit0.read_latch = 0xffff;
		break;
		}
	}

static void write_latch(Bitu port, Bitu val, Bitu /*iolen*/)
	{
	if (pit0.bcd)
		BIN2BCD(pit0.write_latch);
   
	switch (pit0.write_state)
		{
	case 0:
		pit0.write_latch |= ((val&0xff)<<8);
		pit0.write_state = 3;
		break;
	case 1:
		pit0.write_latch = val&0xff;
		break;
	case 2:
		pit0.write_latch = (val&0xff)<<8;
		break;
	case 3:
		pit0.write_latch = val&0xff;
		pit0.write_state = 0;
		break;
		}
	if (pit0.bcd)
		BCD2BIN(pit0.write_latch);
   	if (pit0.write_state != 0)
		{
		if (pit0.write_latch == 0)
			pit0.cntr = pit0.bcd ? 9999 : 0x10000;
		else
			pit0.cntr = pit0.write_latch;

		if (!pit0.new_mode && pit0.mode == 2)
			{
			// In mode 2 writing another value has no direct effect on the count
			// until the old one has run out. This might apply to other modes too.
			// This is not fixed for PIT2 yet!!
			pit0.update_count = true;
			return;
			}
		pit0.start = PIC_FullIndex();
		pit0.delay = (1000.0f/((float)PIT_TICK_RATE/(float)pit0.cntr));

		if (pit0.new_mode || pit0.mode == 0 )
			{
			if (pit0.mode == 0)
				PIC_RemoveEvents(PIT0_Event);		// DoWhackaDo demo
			PIC_AddEvent(PIT0_Event, pit0.delay);
			}
		pit0.new_mode=false;
		}
	}

static Bitu read_latch(Bitu port, Bitu /*iolen*/)
	{
	Bit8u ret = 0;
	if (pit0.go_read_latch == true) 
		counter_latch();
	if( pit0.bcd == true)
		BIN2BCD(pit0.read_latch);
	switch (pit0.read_state)
		{
	case 0:																			// Read MSB & return to state 3
		ret = (pit0.read_latch >> 8) & 0xff;
		pit0.read_state = 3;
		pit0.go_read_latch = true;
		break;
	case 3:																			// Read LSB followed by MSB
		ret = pit0.read_latch & 0xff;
		pit0.read_state = 0;
		break;
	case 1:																			// Read LSB
		ret = pit0.read_latch & 0xff;
		pit0.go_read_latch = true;
		break;
	case 2:																			// Read MSB
		ret = (pit0.read_latch >> 8) & 0xff;
		pit0.go_read_latch = true;
		break;
	default:
		E_Exit("TIMER: Error in readlatch");
		break;
		}
	if( pit0.bcd == true)
		BCD2BIN(pit0.read_latch);
	return ret;
	}

static void write_p43(Bitu /*port*/, Bitu val, Bitu /*iolen*/)
	{
	Bitu latch = (val>>6)&0x03;
	if (latch == 0)
		{
		if ((val & 0x30) == 0)
			counter_latch();														// Counter latch command
		else
			{
			bool old_output = counter_output();										// Save output status to be used with timer 0 irq
			counter_latch();														// Save the current count value to be re-used in undocumented newmode
			pit0.bcd = (val&1)>0;   
			if (val & 1 && pit0.cntr >= 9999)
				pit0.cntr = 9999;
			pit0.start = PIC_FullIndex();											// For undocumented newmode
			pit0.go_read_latch = true;
			pit0.update_count = false;
			pit0.read_state  = (val >> 4) & 0x03;
			pit0.write_state = (val >> 4) & 0x03;
			Bit8u mode = (val>>1)&0x07;
			if (mode > 5)
				mode -= 4;															// 6, 7 become 2 and 3

			pit0.mode = mode;

			/* If the line goes from low to up => generate irq. 
			 *      ( BUT needs to stay up until acknowlegded by the cpu!!! therefore: )
			 * If the line goes to low => disable irq.
			 * Mode 0 starts with a low line. (so always disable irq)
			 * Mode 2,3 start with a high line.
			 * counter_output tells if the current counter is high or low 
			 * So actually a mode 3 timer enables and disables irq al the time. (not handled) */

			PIC_RemoveEvents(PIT0_Event);
			if ((mode != 0) && !old_output)
				PIC_ActivateIRQ(0);
			else
				PIC_DeActivateIRQ(0);
			pit0.new_mode = true;
			}
		}
	}


static IO_ReadHandleObject ReadHandler;
static IO_WriteHandleObject WriteHandler[2];

void TIMER_Init()
	{
	WriteHandler[0].Install(0x40, write_latch);
	WriteHandler[1].Install(0x43, write_p43);
	ReadHandler.Install(0x40, read_latch);
	// Setup Timer 0
	pit0.cntr = 0x10000;
	pit0.write_state = 3;
	pit0.read_state = 3;
	pit0.read_latch = 0;
	pit0.write_latch = 0;
	pit0.mode = 3;
	pit0.bcd = false;
	pit0.go_read_latch = true;
	pit0.update_count = false;
	pit0.delay = (1000.0f/((float)PIT_TICK_RATE/(float)pit0.cntr));

	PIC_AddEvent(PIT0_Event, pit0.delay);
	}
