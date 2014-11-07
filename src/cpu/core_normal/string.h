enum STRING_OP {
	R_OUTSB, R_OUTSW, R_OUTSD,
	R_INSB, R_INSW, R_INSD,
	R_MOVSB, R_MOVSW, R_MOVSD,
	R_LODSB, R_LODSW, R_LODSD,
	R_STOSB, R_STOSW, R_STOSD,
	R_SCASB, R_SCASW, R_SCASD,
	R_CMPSB, R_CMPSW, R_CMPSD
};

#define LoadD(_BLAH) _BLAH
#define R_OPTAT 6
#define R_OPTCOR() if (CPU_Cycles > 0) CPU_Cycles += (count>>1)+(count>>2)-2		// CPU time corrects to ~25%
//#define R_OPTCOR() if (CPU_Cycles > 0) CPU_Cycles += (count*4)/5-2
#define BaseDI SegBase(es)
#define BaseSI BaseDS

static void DoString(STRING_OP type)
	{
	Bitu	si_index, di_index;
	Bitu	add_mask;
	Bitu	count;

	add_mask = AddrMaskTable[core.prefixes&PREFIX_ADDR];
	if (!TEST_PREFIX_REP)
		count = 1;
	else
		{
		count = reg_ecx&add_mask;
		if (count == 0)																// Seems to occur sometimes (calculated CX)
			return;																	// Also required for do...while handling single operations
		CPU_Cycles++;
		if (type < R_SCASB)															// Won't interrupt scas and cmps instruction since they can interrupt themselves
			{																		// So they are also not limited to use cycles!
			if (count > (Bitu)CPU_Cycles)											// Calculate amount of ops to do before cycles run out
				{
				if (count-(Bitu)CPU_Cycles > (Bitu)CPU_CycleMax/16)
					{
					reg_ecx = (reg_ecx&~add_mask)|(count-CPU_Cycles);
					count = CPU_Cycles;
					LOADIP;															// Reset IP to the start
					}
				else
					reg_ecx &= ~add_mask;
				CPU_Cycles = 0;
				}
			else
				{																	// So they are also not limited to use cycles!
				CPU_Cycles -= count;
				reg_ecx &= ~add_mask;
				}
			}
		}
	Bits add_index = cpu.direction;

	switch (type)
		{
	case R_OUTSB:
		si_index = reg_esi&add_mask;
		do
			{
			IO_WriteB(reg_dx, Mem_Lodsb(BaseSI+si_index));
			si_index = (si_index+add_index)&add_mask;
			}
		while (--count);
		reg_esi = (reg_esi&~add_mask)|si_index;
		break;
	case R_OUTSW:
		add_index <<= 1;
		si_index = reg_esi&add_mask;
		do
			{
			IO_WriteW(reg_dx, Mem_Lodsw(BaseSI+si_index));
			si_index = (si_index+add_index)&add_mask;
			}
		while (--count);
		reg_esi = (reg_esi&~add_mask)|si_index;
		break;
	case R_OUTSD:
		add_index <<= 2;
		si_index = reg_esi&add_mask;
		do
			{
			IO_WriteD(reg_dx, Mem_Lodsd(BaseSI+si_index));
			si_index = (si_index+add_index)&add_mask;
			}
		while (--count);
		reg_esi = (reg_esi&~add_mask)|si_index;
		break;
	case R_INSB:
		di_index = reg_edi&add_mask;
		do
			{
			Mem_Stosb(BaseDI+di_index, IO_ReadB(reg_dx));
			di_index = (di_index+add_index)&add_mask;
			}
		while (--count);
		reg_edi = (reg_edi&~add_mask)|di_index;
		break;
	case R_INSW:
		add_index <<= 1;
		di_index = reg_edi&add_mask;
		do
			{
			Mem_Stosw(BaseDI+di_index, IO_ReadW(reg_dx));
			di_index = (di_index+add_index)&add_mask;
			}
		while (--count);
		reg_edi = (reg_edi&~add_mask)|di_index;
		break;
	case R_INSD:
		add_index <<= 2;
		di_index = reg_edi&add_mask;
		do
			{
			Mem_Stosd(BaseDI+di_index, IO_ReadD(reg_dx));
			di_index = (di_index+add_index) & add_mask;
			}
		while (--count);
		reg_edi = (reg_edi&~add_mask)|di_index;
		break;
	case R_MOVSB:
		si_index = reg_esi&add_mask;
		di_index = reg_edi&add_mask;
		if (count >= R_OPTAT && add_index > 0)										// Try to optimize if direction is up (down is used rarely)
			{
			Bitu di_newindex = di_index+(add_index*count)-add_index;
			Bitu si_newindex = si_index+(add_index*count)-add_index;
			if (((di_newindex|si_newindex)&add_mask) == (di_newindex|si_newindex))	// If no wraparounds, use Mem_rMovsb()
				{
				Mem_rMovsb(BaseDI+di_index, BaseSI+si_index, count);
				di_index = (di_newindex+add_index)&add_mask;
				si_index = (si_newindex+add_index)&add_mask;
				R_OPTCOR();
				reg_esi = (reg_esi&~add_mask)|si_index;
				reg_edi = (reg_edi&~add_mask)|di_index;
				break;
				}
			}
		do																			// Count too low or SI/DI wraps around
			{
			Mem_Stosb(BaseDI+di_index, Mem_Lodsb(BaseSI+si_index));
			di_index = (di_index+add_index)&add_mask;
			si_index = (si_index+add_index)&add_mask;
			}
		while (--count);
		reg_esi = (reg_esi&~add_mask)|si_index;
		reg_edi = (reg_edi&~add_mask)|di_index;
		break;
	case R_MOVSW:
		{
		add_index <<= 1;
		si_index = reg_esi&add_mask;
		di_index = reg_edi&add_mask;
		if (count >= R_OPTAT && add_index > 0)										// Try to optimize if direction is up
			{
			Bitu di_newindex = di_index+(add_index*count)-add_index;
			Bitu si_newindex = si_index+(add_index*count)-add_index;
			if (((di_newindex|si_newindex)&add_mask) == (di_newindex|si_newindex))	// If no wraparounds, use Mem_rMovsb()
				{
//				Mem_rMovsw(BaseDI+di_index, BaseSI+si_index, count);
				Mem_rMovsb(BaseDI+di_index, BaseSI+si_index, count*2);
				di_index = (di_newindex+add_index)&add_mask;
				si_index = (si_newindex+add_index)&add_mask;
				R_OPTCOR();
				reg_esi = (reg_esi&~add_mask)|si_index;
				reg_edi = (reg_edi&~add_mask)|di_index;
				break;
				}
			}
		do																			// Count too low or SI/DI wraps around
			{
			Mem_Stosw(BaseDI+di_index, Mem_Lodsw(BaseSI+si_index));
			di_index = (di_index+add_index)&add_mask;
			si_index = (si_index+add_index)&add_mask;
			}
		while (--count);
		}
		reg_esi = (reg_esi&~add_mask)|si_index;
		reg_edi = (reg_edi&~add_mask)|di_index;
		break;
	case R_MOVSD:
		add_index <<= 2;
		si_index = reg_esi&add_mask;
		di_index = reg_edi&add_mask;
		if (count >= R_OPTAT && add_index > 0)										// Try to optimize if direction is up
			{
			Bitu di_newindex = di_index+(add_index*count)-add_index;
			Bitu si_newindex = si_index+(add_index*count)-add_index;
			if (((di_newindex|si_newindex)&add_mask) == (di_newindex|si_newindex))	// If no wraparounds, use Mem_rMovsb()
				{
//				Mem_rMovsd(BaseDI+di_index, BaseSI+si_index, count);
				Mem_rMovsb(BaseDI+di_index, BaseSI+si_index, count*4);
				di_index = (di_newindex+add_index)&add_mask;
				si_index = (si_newindex+add_index)&add_mask;
				R_OPTCOR();
				reg_esi = (reg_esi&~add_mask)|si_index;
				reg_edi = (reg_edi&~add_mask)|di_index;
				break;
				}
			}
		do
			{
			Mem_Stosd(BaseDI+di_index, Mem_Lodsd(BaseSI+si_index));
			di_index = (di_index+add_index)&add_mask;
			si_index = (si_index+add_index)&add_mask;
			}
		while (--count);
		reg_esi = (reg_esi&~add_mask)|si_index;
		reg_edi = (reg_edi&~add_mask)|di_index;
		break;
	case R_LODSB:
		si_index = reg_esi&add_mask;
		do
			{
			reg_al = Mem_Lodsb(BaseSI+si_index);
			si_index = (si_index+add_index)&add_mask;
			}
		while (--count);
		reg_esi = (reg_esi&~add_mask)|si_index;
		break;
	case R_LODSW:
		add_index <<= 1;
		si_index = reg_esi&add_mask;
		do
			{
			reg_ax = Mem_Lodsw(BaseSI+si_index);
			si_index = (si_index+add_index)&add_mask;
			}
		while (--count);
		reg_esi = (reg_esi&~add_mask)|si_index;
		break;
	case R_LODSD:
		add_index <<= 2;
		si_index = reg_esi&add_mask;
		do
			{
			reg_eax = Mem_Lodsd(BaseSI+si_index);
			si_index = (si_index+add_index)&add_mask;
			}
		while (--count);
		reg_esi = (reg_esi&~add_mask)|si_index;
		break;
	case R_STOSB:
		{
		di_index = reg_edi&add_mask;
		if (count >= R_OPTAT)														// Try to optimize
			{
			Bitu di_newindex = di_index+(add_index*count)-add_index;
			if ((di_newindex&add_mask) == di_newindex)								// If no wraparound, use Mem_rStosb()
				{
				Mem_rStosb(BaseDI+(add_index > 0 ? di_index : di_newindex), reg_al, count);
				di_index = (di_newindex+add_index)&add_mask;
				R_OPTCOR();
				reg_edi = (reg_edi&~add_mask)|di_index;
				break;
				}
			}
		do																			// Count too low or DI wraps around
			{
			Mem_Stosb(BaseDI+di_index, reg_al);
			di_index = (di_index+add_index)&add_mask;
			}
		while (--count);
		}
		reg_edi = (reg_edi&~add_mask)|di_index;
		break;
	case R_STOSW:
		add_index <<= 1;
		di_index = reg_edi&add_mask;
		if (count >= R_OPTAT)														// Try to optimize
			{
			Bitu di_newindex = di_index+(add_index*count)-add_index;
			if ((di_newindex&add_mask) == di_newindex)								// If no wraparound, use Mem_rStosw()
				{
				if (reg_al == reg_ah)												// NB, memset runtime is 32 bits optimized
					Mem_rStosb(BaseDI+(add_index > 0 ? di_index : di_newindex), reg_al, count*2);
				else
					Mem_rStosw(BaseDI+(add_index > 0 ? di_index : di_newindex), reg_ax, count);
				di_index = (di_newindex+add_index)&add_mask;
				R_OPTCOR();
				reg_edi = (reg_edi&~add_mask)|di_index;
				break;
				}
			}
		do																			// Count too low or DI wraps around
			{
			Mem_Stosw(BaseDI+di_index, reg_ax);
			di_index = (di_index+add_index)&add_mask;
			}
		while (--count);
		reg_edi = (reg_edi&~add_mask)|di_index;
		break;
	case R_STOSD:
		add_index <<= 2;
		di_index = reg_edi&add_mask;
		if (count >= R_OPTAT)														// Try to optimize
			{
			Bitu di_newindex = di_index+(add_index*count)-add_index;
			if ((di_newindex&add_mask) == di_newindex)								// If no wraparound, use Mem_Stosd()
				{
				if ((reg_eax>>16) == reg_ax && reg_ah == reg_al)					// NB, memset runtime is 32 bits optimized
					Mem_rStosb(BaseDI+(add_index > 0 ? di_index : di_newindex), reg_al, count*4);
				else
					Mem_rStosd(BaseDI+(add_index > 0 ? di_index : di_newindex), reg_eax, count);
				di_index = (di_newindex+add_index)&add_mask;
				R_OPTCOR();
				reg_edi = (reg_edi&~add_mask)|di_index;
				break;
				}
			}
		do
			{
			Mem_Stosd(BaseDI+di_index, reg_eax);
			di_index = (di_index+add_index)&add_mask;
			}
		while (--count);
		reg_edi = (reg_edi&~add_mask)|di_index;
		break;
	case R_SCASB:
		{
		di_index = reg_edi&add_mask;
		Bit8u val2;
		CPU_Cycles -= count;
		do
			{
			val2 = Mem_Lodsb(BaseDI+di_index);
			di_index = (di_index+add_index)&add_mask;
			}
		while (--count && (reg_al == val2) == core.rep_zero);
		CPU_Cycles += count;
		reg_edi = (reg_edi&~add_mask)|di_index;
		if (TEST_PREFIX_REP)
			reg_ecx = (reg_ecx&~add_mask)|count;
		CMPB(reg_al, val2, LoadD, 0);
		}
		break;
	case R_SCASW:
		{
		add_index <<= 1;
		di_index = reg_edi&add_mask;
		Bit16u val2;
		CPU_Cycles -= count;
		do
			{
			val2 = Mem_Lodsw(BaseDI+di_index);
			di_index = (di_index+add_index)&add_mask;
			}
		while (--count && (reg_ax == val2) == core.rep_zero);
		CPU_Cycles += count;
		reg_edi = (reg_edi&~add_mask)|di_index;
		if (TEST_PREFIX_REP)
			reg_ecx = (reg_ecx&~add_mask)|count;
		CMPW(reg_ax, val2, LoadD, 0);
		}
		break;
	case R_SCASD:
		{
		add_index <<= 2;
		di_index = reg_edi&add_mask;
		Bit32u val2;
		CPU_Cycles -= count;
		do
			{
			val2 = Mem_Lodsd(BaseDI+di_index);
			di_index = (di_index+add_index)&add_mask;
			}
		while (--count && (reg_eax == val2) == core.rep_zero);
		CPU_Cycles += count;
		reg_edi = (reg_edi&~add_mask)|di_index;
		if (TEST_PREFIX_REP)
			reg_ecx = (reg_ecx&~add_mask)|count;
		CMPD(reg_eax, val2, LoadD, 0);
		}
		break;
	case R_CMPSB:
		{
		si_index = reg_esi&add_mask;
		di_index = reg_edi&add_mask;
		Bit8u val1, val2;
		CPU_Cycles -= count;
		do
			{
			val1 = Mem_Lodsb(BaseSI+si_index);
			val2 = Mem_Lodsb(BaseDI+di_index);
			si_index = (si_index+add_index)&add_mask;
			di_index = (di_index+add_index)&add_mask;
			}
		while (--count && (val1 == val2) == core.rep_zero);
		CPU_Cycles += count;
		reg_esi = (reg_esi&~add_mask)|si_index;
		reg_edi = (reg_edi&~add_mask)|di_index;
		if (TEST_PREFIX_REP)
			reg_ecx = (reg_ecx&~add_mask)|count;
		CMPB(val1, val2, LoadD, 0);
		}
		break;
	case R_CMPSW:
		{
		add_index <<= 1;
		si_index = reg_esi&add_mask;
		di_index = reg_edi&add_mask;
		Bit16u val1, val2;
		CPU_Cycles -= count;
		do
			{
			val1 = Mem_Lodsw(BaseSI+si_index);
			val2 = Mem_Lodsw(BaseDI+di_index);
			si_index = (si_index+add_index)&add_mask;
			di_index = (di_index+add_index)&add_mask;
			}
		while (--count && (val1 == val2) == core.rep_zero);
		CPU_Cycles += count;
		reg_esi = (reg_esi&~add_mask)|si_index;
		reg_edi = (reg_edi&~add_mask)|di_index;
		if (TEST_PREFIX_REP)
			reg_ecx = (reg_ecx&~add_mask)|count;
		CMPW(val1, val2, LoadD, 0);
		}
		break;
	case R_CMPSD:
		{
		add_index <<= 2;
		si_index = reg_esi&add_mask;
		di_index = reg_edi&add_mask;
		Bit32u val1, val2;
		CPU_Cycles -= count;
		do
			{
			val1 = Mem_Lodsd(BaseSI+si_index);
			val2 = Mem_Lodsd(BaseDI+di_index);
			si_index = (si_index+add_index)&add_mask;
			di_index = (di_index+add_index)&add_mask;
			}
		while (--count && (val1 == val2) == core.rep_zero);
		CPU_Cycles += count;
		reg_esi = (reg_esi&~add_mask)|si_index;
		reg_edi = (reg_edi&~add_mask)|di_index;
		if (TEST_PREFIX_REP)
			reg_ecx = (reg_ecx&~add_mask)|count;
		CMPD(val1, val2, LoadD, 0);
		}
		break;
		}
	}
