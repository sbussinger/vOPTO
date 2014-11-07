#include "vDos.h"
#include "inout.h"


static Bitu dummy_read(Bitu /*port*/, Bitu /*iolen*/)
	{
	return 0;
	}	

static void dummy_write(Bitu /*port*/, Bitu /*val*/, Bitu /*iolen*/)
	{
	}

static Bitu read_p64(Bitu port,Bitu iolen)
	{
	return 0x1d;																	// Just to get rid of of all these reads
	}


void KEYBOARD_Init()
	{
	IO_RegisterWriteHandler(0x60, dummy_write);
	IO_RegisterWriteHandler(0x61, dummy_write);
	IO_RegisterWriteHandler(0x64, dummy_write);
	IO_RegisterReadHandler(0x60, dummy_read);
	IO_RegisterReadHandler(0x61, dummy_read);
	IO_RegisterReadHandler(0x64, read_p64);
	}
