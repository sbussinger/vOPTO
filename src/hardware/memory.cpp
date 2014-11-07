#include "vDos.h"
#include "mem.h"
#include "inout.h"
#include "paging.h"
#include "cpu.h"
#include "bios.h"

static Bit8u a20_controlport;
static PageHandler * pageHandlers[MEM_PAGES];

HostPt MemBase;

class RAMPageHandler : public PageHandler
	{
public:
	RAMPageHandler()
		{
		flags = PFLAG_READABLE|PFLAG_WRITEABLE;
		}
	Bit8u readb(PhysPt addr)
		{
		return *(MemBase+addr);
		}
	Bit16u readw(PhysPt addr)
		{
		return *(Bit16u*)(MemBase+addr);
		}
	Bit32u readd(PhysPt addr)
		{
		return *(Bit32u*)(MemBase+addr);
		}
	void writeb(PhysPt addr, Bit8u val)
		{
		*(MemBase+addr) = val;
		}
	void writew(PhysPt addr, Bit16u val)
		{
		*(Bit16u*)(MemBase+addr) = val;
		}
	void writed(PhysPt addr, Bit32u val)
		{
		*(Bit32u*)(MemBase+addr) = val;
		}
	HostPt GetHostPt(PhysPt addr)
		{
		return MemBase+addr;
		}
	};

class ROMPageHandler : public RAMPageHandler
	{
public:
	ROMPageHandler()
		{
		flags = PFLAG_READABLE|PFLAG_HASROM;
		}
	void writeb(PhysPt addr, Bit8u val)
		{
		}
	void writew(PhysPt addr, Bit16u val)
		{
		}
	void writed(PhysPt addr, Bit32u val)
		{
		}
	};

static RAMPageHandler ram_page_handler;
static ROMPageHandler rom_page_handler;

#define TLB_SIZE	65536															// Full TLB cache (Note: for LinToPhys() to function w/o masks and a second table,it has to be 20 bits)
static Bit32u TLB_phys[TLB_SIZE];

void clearTLB(void)
	{
	memset(TLB_phys, 0xff, TLB_SIZE*4);
	}

PhysPt LinToPhys(LinPt addr)
	{
	int TLB_idx = addr>>12;
	if (TLB_phys[TLB_idx] == -1)
		{
		Bit32u pdAddr =(TLB_idx>>8)&0xffc;
		Bit32u pdEntry = *(Bit32u *)(MemBase+PAGING_GetDirBase()+pdAddr);
		if (!(pdEntry&1))
			E_Exit("Page fault: directory entry not present: %x", addr);
		Bit32u ptAddr = (pdEntry&~0xfff)+((TLB_idx<<2)&0xffc);
		Bit32u ptEntry = *(Bit32u *)(MemBase+ptAddr);
		if (!(ptEntry&1))
			E_Exit("Page fault: table entry not present: %x", addr);
		TLB_phys[TLB_idx] = ptEntry&~0xfff;
		}
	return TLB_phys[TLB_idx]+(addr&0xfff);
	}

PageHandler * MEM_GetPageHandler(PhysPt addr)
	{
	if (addr < TOT_MEM_BYTES)
		return pageHandlers[addr/MEM_PAGESIZE];
	E_Exit("Access to invalid memory location %x", addr);
	return 0;
	}

void MEM_SetPageHandler(Bitu phys_page, Bitu pages, PageHandler * handler)
	{
	for (; pages > 0; pages--)
		pageHandlers[phys_page++] = handler;
	}

void MEM_ResetPageHandler(Bitu phys_page, Bitu pages)
	{
	for (; pages > 0; pages--)
		pageHandlers[phys_page++] = &ram_page_handler;
	}


Bit8u Mem_Lodsb(LinPt addr)
	{
	if (PAGING_Enabled())
		addr = LinToPhys(addr);
	if (addr < 0xa0000 || addr > 0xfffff)											// If in lower or extended mem, read direct (always readable)
		{
		if ((addr <= BIOS_KEYBOARD_BUFFER_TAIL && addr >= BIOS_KEYBOARD_FLAGS1))// || addr == BIOS_KEYBOARD_FLAGS3)		// Access to keyboard info
			{
			if (winHidden && !winHide10th)											// Unhide window on access
				{
				hideWinTill = GetTickCount();
				winHide10th = 1;													// It would kept to be delayed
				}
			else
				idleCount++;
			}
		return *(Bit8u *)(MemBase+addr);
		}
	PageHandler * ph = MEM_GetPageHandler(addr);
	if (ph->flags&PFLAG_READABLE)
		return *(Bit8u *)(ph->GetHostPt(addr));
	return ph->readb(addr);
	}

Bit16u Mem_Lodsw(LinPt addr)
	{
	if (PAGING_Enabled())
		addr = LinToPhys(addr);
	if (addr < 0x9ffff || addr > 0xfffff)											// If in lower or extended mem, read direct (always readable)
		{
		if (addr <= BIOS_KEYBOARD_BUFFER_TAIL && addr >= BIOS_KEYBOARD_FLAGS1)		// Access to keyboard info
			{
			if (winHidden && !winHide10th)											// Unhide window on access
				{
				hideWinTill = GetTickCount();
				winHide10th = 1;													// It would kept to be delayed
				}
			else
				idleCount++;
			}
		return *(Bit16u *)(MemBase+addr);
		}
	if ((addr&(MEM_PAGESIZE-1)) != (MEM_PAGESIZE-1))
		{
		PageHandler * ph = MEM_GetPageHandler(addr);
		if (ph->flags&PFLAG_READABLE)
			return *(Bit16u *)(ph->GetHostPt(addr));
		else
			return ph->readw(addr);
		}
	return Mem_Lodsb(addr) | (Mem_Lodsb(addr+1) << 8);
	}

Bit32u Mem_Lodsd(LinPt addr)
	{
	if (PAGING_Enabled())
		addr = LinToPhys(addr);
	if (addr < 0x9fffd || addr > 0xfffff)											// If in lower or exyended mem, read direct (always readable)
		return *(Bit32u *)(MemBase+addr);
	if ((addr&(MEM_PAGESIZE-1)) < (MEM_PAGESIZE-3))
		{
		PageHandler * ph = MEM_GetPageHandler(addr);
		if (ph->flags&PFLAG_READABLE)
			return *(Bit32u *)(ph->GetHostPt(addr));
		else
			return ph->readd(addr);
		}
	return Mem_Lodsw(addr) | (Mem_Lodsw(addr+2) << 16);
	}

void Mem_Stosb(LinPt addr, Bit8u val)
	{
	if (PAGING_Enabled())
		addr = LinToPhys(addr);
	if (addr < 0xa0000 || addr > 0xfffff)											// If in lower or extended mem, write direct (always writeable)
		{
		*(MemBase+addr) = val;
		return;
		}
	PageHandler * ph = MEM_GetPageHandler(addr);
	if (ph->flags&PFLAG_WRITEABLE)
		*(Bit8u *)(ph->GetHostPt(addr)) = val;
	else
		ph->writeb(addr, val);
	}

void Mem_Stosw(LinPt addr, Bit16u val)
	{
	if (PAGING_Enabled())
		addr = LinToPhys(addr);
	if (addr < 0x9ffff || addr > 0xfffff)											// If in lower or extended mem, write direct (always writeable)
		{
		*(Bit16u *)(MemBase+addr) = val;
		return;
		}
	if ((addr&(MEM_PAGESIZE-1)) < (MEM_PAGESIZE-1))
		{
		PageHandler * ph = MEM_GetPageHandler(addr);
		if (ph->flags&PFLAG_WRITEABLE)
			*(Bit16u *)(ph->GetHostPt(addr)) = val;
		else
			ph->writew(addr, val);
		}
	else
		{
		Mem_Stosb(addr, val&0xff);
		Mem_Stosb(addr+1, val>>8);
		}
	}

void Mem_Stosd(LinPt addr, Bit32u val)
	{
	if (PAGING_Enabled())
		addr = LinToPhys(addr);
	if (addr < 0x9fffd || addr > 0xfffff)											// If in lower mem, write direct (always writeable)
		{
		*(Bit32u *)(MemBase+addr) = val;
		return;
		}
	if ((addr&(MEM_PAGESIZE-1)) < (MEM_PAGESIZE-3))
		{
		PageHandler * ph = MEM_GetPageHandler(addr);
		if (ph->flags&PFLAG_WRITEABLE)
			*(Bit32u *)(ph->GetHostPt(addr)) = val;
		else
			ph->writed(addr, val);
		}
	else
		{
		Mem_Stosw(addr, val&0xffff);
		Mem_Stosw(addr+2, val>>16);
		}
	}

void Mem_rMovsb(LinPt dest, LinPt src, Bitu bCount)
	{
	Bit16u maxMove = PAGING_Enabled() ? 4096 : MEM_PAGESIZE;						// If paging, use 4096 bytes page size (strictly not needed?)
	while (bCount)																	// Set in chunks of MEM_PAGESIZE for mem mapping to take effect
		{
		PhysPt physSrc = PAGING_Enabled() ? LinToPhys(src) : src;
		PhysPt physDest = PAGING_Enabled() ? LinToPhys(dest) : dest;
		Bit16u srcOff = ((Bit16u)physSrc)&(maxMove-1);
		Bit16u destOff = ((Bit16u)physDest)&(maxMove-1);
		Bit16u bTodo = maxMove - max(srcOff, destOff);
		if (bTodo > bCount)
			bTodo = bCount;
		src += bTodo;
		dest += bTodo;
		bCount -= bTodo;
		PageHandler * phSrc = MEM_GetPageHandler(physSrc);
		PageHandler * phDest = MEM_GetPageHandler(physDest);

		if (phDest->flags&PFLAG_WRITEABLE && phSrc->flags&PFLAG_READABLE)
			{
			Bit8u *hSrc = phSrc->GetHostPt(physSrc);
			Bit8u *hDest = phDest->GetHostPt(physDest);
			if ((hSrc <= hDest && hSrc+bTodo > hDest) || (hDest <= hSrc && hDest+bTodo > hSrc))
				while (bTodo--)														// If source and destination overlap, do it "by hand"
					*(hDest++) = *(hSrc++);											// memcpy() messes things up in another way than rep movsb does!
			else
				memcpy(hDest, hSrc, bTodo);
			}
		else																		// Not writeable, or use (VGA)handler
			while (bTodo--)
				phDest->writeb(physDest++, phSrc->readb(physSrc++));
		}
	}

void Mem_rMovsw(LinPt dest, LinPt src, Bitu wCount)
	{
	Bit16u maxMove = PAGING_Enabled() ? 4096 : MEM_PAGESIZE;						// If paging, use 4096 bytes page size (strictly not needed?)
	Bitu bCount = wCount<<1;														// Have to use a byte count (words can be split over pages)
	while (bCount)																	// Move in chunks of MEM_PAGESIZE for mapping to take effect
		{
		PhysPt physSrc = PAGING_Enabled() ? LinToPhys(src) : src;
		PhysPt physDest = PAGING_Enabled() ? LinToPhys(dest) : dest;
		Bit16u srcOff = ((Bit16u)physSrc)&(maxMove-1);
		Bit16u destOff = ((Bit16u)physDest)&(maxMove-1);
		Bit16u bTodo = maxMove - max(srcOff, destOff);
		if (bTodo > bCount)
			bTodo = bCount;
		bCount -= bTodo;
		Bit16u wTodo = bTodo>>1;
		src += bTodo;
		dest += bTodo;
		PageHandler * phSrc = MEM_GetPageHandler(physSrc);
		PageHandler * phDest = MEM_GetPageHandler(physDest);

		if (phDest->flags&PFLAG_WRITEABLE && phSrc->flags&PFLAG_READABLE)
			{
			Bit8u *hSrc = phSrc->GetHostPt(physSrc);
			Bit8u *hDest = phDest->GetHostPt(physDest);
			if ((hSrc <= hDest && hSrc+bTodo > hDest) || (hDest <= hSrc && hDest+bTodo > hSrc))
				{
				while (wTodo--)														// If source and destination overlap, do it "by hand"
					{
				 	*(Bit16u *)hDest = *(Bit16u *)hSrc;								// memcpy() messes things up in another way than rep movsw does!
					hSrc += 2;
					hDest += 2;
					}
				if (bTodo&1)
					*hDest = *hSrc;													// One byte left in this page
				}
			else
				memcpy(hDest, hSrc, bTodo);											// memcpy() is optimized for 32 bits
			}
		else																		// Not writeable, or use (VGA)handler
			{
			while (wTodo--)
				{
				phDest->writew(physDest, phSrc->readw(physSrc));
				physDest += 2;
				physSrc += 2;
				}
			if (bTodo&1)
				phDest->writeb(physDest, phSrc->readb(physSrc));
			}
		}
	}

void Mem_rMovsd(LinPt dest, LinPt src, Bitu dCount)
	{
	Bit16u maxMove = PAGING_Enabled() ? 4096 : MEM_PAGESIZE;						// If paging, use 4096 bytes page size (strictly not needed?)
	Bitu bCount = dCount<<2;														// Have to use a byte count (dwords can be split over pages)
	while (bCount)																	// Move in chunks of MEM_PAGESIZE for mapping to take effect
		{
		PhysPt physSrc = PAGING_Enabled() ? LinToPhys(src) : src;
		PhysPt physDest = PAGING_Enabled() ? LinToPhys(dest) : dest;
		Bit16u srcOff = ((Bit16u)physSrc)&(maxMove-1);
		Bit16u destOff = ((Bit16u)physDest)&(maxMove-1);
		Bit16u bTodo = maxMove - max(srcOff, destOff);
		if (bTodo > bCount)
			bTodo = bCount;
		bCount -= bTodo;
		Bit32u dTodo = bTodo>>2;
		src += bTodo;
		dest += bTodo;
		PageHandler * phSrc = MEM_GetPageHandler(physSrc);
		PageHandler * phDest = MEM_GetPageHandler(physDest);

		if (phDest->flags&PFLAG_WRITEABLE && phSrc->flags&PFLAG_READABLE)
			{
			Bit8u *hSrc = phSrc->GetHostPt(physSrc);
			Bit8u *hDest = phDest->GetHostPt(physDest);
			if ((hSrc <= hDest && hSrc+bTodo > hDest) || (hDest <= hSrc && hDest+bTodo > hSrc))
				{
				while (dTodo--)														// If source and destination overlap, do it "by hand"
					{
				 	*(Bit32u*)hDest = *(Bit32u*)hSrc;								// memcpy() messes things up in another way than rep movsw does!
					hSrc += 4;
					hDest += 4;
					}
				if (bTodo&2)
					{
					*(Bit16u*)hDest = *(Bit16u*)hSrc;								// One word left in this page
					hSrc += 2;
					hDest += 2;
					}
				if (bTodo&1)
					*hDest = *hSrc;													// One byte left in this page
				}
			else
				memcpy(hDest, hSrc, bTodo);											// memcpy() is optimized for 32 bits
			}
		else																		// Not writeable, or use (VGA)handler
			{
			while (dTodo--)
				{
				phDest->writed(physDest, phSrc->readd(physSrc));
				physDest += 4;
				physSrc += 4;
				}
			if (bTodo&2)
				{
				phDest->writew(physDest, phSrc->readw(physSrc));
				physDest += 2;
				physSrc += 2;
				}
			if (bTodo&1)
				phDest->writeb(physDest, phSrc->readb(physSrc));
			}
		}
	}

Bitu Mem_StrLen(LinPt addr)
	{
	for (Bitu len = 0; len < 65536; len++)
		if (!Mem_Lodsb(addr+len))
			return len;
	return 0;																		// This shouldn't happen
	}

void Mem_CopyTo(LinPt dest, void const * const src, Bitu bCount)
	{
	Bit8u const * srcAddr = (Bit8u const *)src;
	Bit16u maxMove = PAGING_Enabled() ? 4096 : MEM_PAGESIZE;						// If paging, use 4096 bytes page size (strictly not needed?)
	while (bCount)
		{
		PhysPt physDest = PAGING_Enabled() ? LinToPhys(dest) : dest;
		Bit16u bTodo = maxMove-(((Bit16u)physDest)&(maxMove-1));
		if (bTodo > bCount)
			bTodo = bCount;
		bCount -= bTodo;
		dest += bTodo;
		PageHandler * ph = MEM_GetPageHandler(physDest);
		if (ph->flags&PFLAG_WRITEABLE)
			{
			memcpy(ph->GetHostPt(physDest), srcAddr, bTodo);
			srcAddr += bTodo;
			}
		else
			while (bTodo--)
				ph->writeb(physDest++, *srcAddr++);
		}
	}

void Mem_CopyFrom(LinPt src, void * dest, Bitu bCount)
	{
	Bit8u * destAddr = (Bit8u *)dest;
	Bit16u maxMove = PAGING_Enabled() ? 4096 : MEM_PAGESIZE;						// If paging, use 4096 bytes page size (strictly not needed?)
	while (bCount)
		{
		PhysPt physSrc = PAGING_Enabled() ? LinToPhys(src) : src;
		Bit16u bTodo = maxMove - (((Bit16u)physSrc)&(maxMove-1));
		if (bTodo > bCount)
			bTodo = bCount;
		bCount -= bTodo;
		src += bTodo;
		PageHandler * ph = MEM_GetPageHandler(physSrc);
		if (ph->flags & PFLAG_READABLE)
			{
			memcpy(destAddr, ph->GetHostPt(physSrc), bTodo);
			destAddr += bTodo;
			}
		else
			while (bTodo--)
				*destAddr++ = ph->readb(physSrc++);
		}
	}

void Mem_StrnCopyFrom(char * data, LinPt pt, Bitu bCount)
	{
	while (bCount--)
		{
		Bit8u c = Mem_Lodsb(pt++);
		if (!c)
			break;
		*data++ = c;
		}
	*data = 0;
	}

void Mem_rStos4b(LinPt addr, Bit32u val32, Bitu bCount)								// Used by rStosw and rStosd (bytes are not the same)
	{
	Bit16u maxMove = PAGING_Enabled() ? 4096 : MEM_PAGESIZE;						// If paging, use 4096 bytes page size (strictly not needed?)
	while (bCount)																	// Set in chunks of MEM_PAGESIZE for mem mapping to take effect
		{
		PhysPt physAddr = PAGING_Enabled() ? LinToPhys(addr) : addr;
		Bit16u bTodo = maxMove - (((Bit16u)physAddr)&(maxMove-1));
		if (bTodo > bCount)
			bTodo = bCount;
		PageHandler *ph = MEM_GetPageHandler(physAddr);
		addr += bTodo;
		if (ph->flags&PFLAG_WRITEABLE)
			{
			HostPt hPtr = ph->GetHostPt(physAddr);
			while ((Bit32u)(hPtr) & 3)												// Align start address to 32 bit
				{
				*(hPtr++) = val32&0xff;
				val32 = _rotr(val32, 8);
				bTodo--;
				bCount--;
				}
			bCount -= bTodo&~3;
			for (bTodo >>= 2; bTodo; bTodo--)										// Set remaing with 32 bit value
				{
				*(Bit32u *)hPtr = val32;
				hPtr += 4;
				}
			if (bCount < 4)															// Eventually last remaining bytes
				{
				while (bCount--)
					{
					*hPtr++ = val32&0xff;
					val32 >>= 8;
					}
				return;
				}
			}
		else																		// Not writeable, or use (VGA)handler
			{
			while ((Bit32u)(physAddr) & 3)											// Align start address to 32 bit
				{
				ph->writeb(physAddr++, val32&0xff);
				val32 = _rotr(val32, 8);
				bTodo--;
				bCount--;
				}
			bCount -= bTodo&~3;
			for (bTodo >>= 2; bTodo; bTodo--)										// Set remaing with 32 bit value
				{
				ph->writed(physAddr, val32);
				physAddr += 4;
				}
			if (bCount < 4)															// Eventually last remaining bytes
				{
				while (bCount--)
					{
					ph->writeb(physAddr++, val32&0xff);
					val32 >>= 8;
					}
				return;
				}
			}
		}
	}

void Mem_rStosb(LinPt addr, Bit8u val, Bitu count)
	{
	Bit16u maxMove = PAGING_Enabled() ? 4096 : MEM_PAGESIZE;						// If paging, use 4096 bytes page size (strictly not needed?)
	while (count)																	// Set in chunks of MEM_PAGESIZE for mem mapping to take effect
		{
		PhysPt physAddr = PAGING_Enabled() ? LinToPhys(addr) : addr;
		Bit16u bTodo = maxMove - (((Bit16u)physAddr)&(maxMove-1));
		if (bTodo > count)
			bTodo = count;
		count -= bTodo;
		addr += bTodo;
		PageHandler *ph = MEM_GetPageHandler(physAddr);
		if (ph->flags&PFLAG_WRITEABLE)
			memset(ph->GetHostPt(physAddr), val, bTodo);							// memset() is optimized for 32 bit
		else																		// Not writeable, or use (VGA)handler
			while (bTodo--)
				ph->writeb(physAddr++, val);
		}
	}

static void write_p92(Bitu port, Bitu val, Bitu iolen)
	{	
	if (val&1)																		// Bit 0 = system reset (switch back to real mode)
		E_Exit("CPU reset via port 0x92 not supported.");
	a20_controlport = val & ~2;
	}

static Bitu read_p92(Bitu port, Bitu iolen)
	{
	return a20_controlport;
	}

static IO_ReadHandleObject ReadHandler;
static IO_WriteHandleObject WriteHandler;

void MEM_Init()
	{
	MemBase = (Bit8u *)malloc(TOT_MEM_BYTES);										// Setup the physical memory
	if (!MemBase)
		E_Exit("Can't allocate main memory of %d MB", TOT_MEM_MB);
	memset((void*)MemBase, 0, TOT_MEM_BYTES);										// Clear the memory
		
	for (Bitu i = 0; i < MEM_PAGES; i++)
		pageHandlers[i] = &ram_page_handler;
	for (Bitu i = 0xc0000/MEM_PAGESIZE; i < 0xc4000/MEM_PAGESIZE; i++)				// Setup rom at 0xc0000-0xc3fff
		pageHandlers[i] = &rom_page_handler;
	for (Bitu i = 0xf0000/MEM_PAGESIZE; i < 0x100000/MEM_PAGESIZE; i++)				// Setup rom at 0xf0000-0xfffff
		pageHandlers[i] = &rom_page_handler;
	WriteHandler.Install(0x92, write_p92);											// (Dummy) A20 Line - PS/2 system control port A
	ReadHandler.Install(0x92, read_p92);
	}
