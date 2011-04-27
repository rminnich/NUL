/*
 * Hypervisor Information Page (HIP)
 *
 * Copyright (C) 2008, Udo Steinberg <udo@hypervisor.org>
 * Copyright (C) 2008-2010, Bernhard Kauer <bk@vmmon.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of NUL.
 *
 * NUL is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NUL is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
 * License version 2 for more details.
 */

#pragma once

class Hip_cpu
{
    public:
        unsigned char  flags;
        unsigned char  thread;
        unsigned char  core;
        unsigned char  package;
        unsigned  reserved;
};

class Hip_mem
{
    public:
        unsigned long long  addr;
        unsigned long long  size;
        int     type;
        unsigned  aux;
};

class Hip
{
    public:
        unsigned  signature;
        unsigned short  checksum;       // HIP checksum
        unsigned short  length;         // HIP length
        unsigned short  cpu_offs;       // Offset of first CPU descriptor
        unsigned short  cpu_size;
        unsigned short  mem_offs;       // Offset of first MEM descriptor
        unsigned short  mem_size;
        unsigned  api_flg;        // API feature flags
        unsigned  api_ver;        // API version
        unsigned  cfg_cap;        // Number of CAPs (SEL)
        unsigned  cfg_exc;        // Number of Exception portals (EXC)
        unsigned  cfg_vm;         // Number of VM portals (VMI)
        unsigned  cfg_gsi;        // Number of GSIs
        unsigned  cfg_page;       // PAGE sizes
        unsigned  cfg_utcb;       // UTCB sizes
        unsigned  freq_tsc;       // TSC freq in khz
        unsigned  freq_bus;       // BUS freq in khz


	unsigned short calc_checksum()
	{
	  unsigned short res = 0;
	  for (unsigned i=0; i < length / sizeof(unsigned short); i++)
	    res += reinterpret_cast<unsigned short *>(this)[i];
	  return res;
	}
	void fix_checksum() { checksum -= calc_checksum(); }

	Hip_mem *get_mod(unsigned nr) {
	  for (int i=0; mem_size && i < (length - mem_offs) / mem_size; i++)
	    {
	      Hip_mem *hmem = reinterpret_cast<Hip_mem *>(reinterpret_cast<char *>(this) + mem_offs + i * mem_size);
	      if (hmem->type != -2) continue;
	      if (!nr--) return hmem;
	    }
	  return 0;
	}

	void append_mem(unsigned long long addr, unsigned long long size, int type, unsigned aux)
	{
	  Hip_mem * mem = reinterpret_cast<Hip_mem *>(reinterpret_cast<char *>(this) + length);
	  length += mem_size;
	  mem->addr = addr;
	  mem->size = size;
	  mem->type = type;
	  mem->aux = aux;
	}

	bool has_vmx() {  return api_flg &  (1 << 1); }
	bool has_svm() {  return api_flg &  (1 << 2); }

	// Returns the number of Hip_cpu descriptors.
	unsigned cpu_desc_count() const { return (mem_offs - cpu_offs) / cpu_size; }

	// Returns the number of enabled CPUs.
	unsigned cpu_count() {
	  unsigned cpucnt = 0;
	  for (unsigned i=0; i < cpu_desc_count(); i++) {
	    Hip_cpu *cpu = reinterpret_cast<Hip_cpu *>(reinterpret_cast<char *>(this) + cpu_offs + i*cpu_size);
	    if (~cpu->flags & 1) continue;
	    cpucnt++;
	  }
	  return cpucnt;
	}

	Hip_cpu *cpus() { return reinterpret_cast<Hip_cpu *>(reinterpret_cast<char *>(this) + cpu_offs); }

	// Maps a logical CPU number to an index into the Hip_cpu array.
	unsigned cpu_physical (unsigned logical) {
	  logical %= cpu_count();
	  for (unsigned i=0; i < cpu_desc_count(); i++) {
	    Hip_cpu *cpu = reinterpret_cast<Hip_cpu *>(reinterpret_cast<char *>(this) + cpu_offs + i*cpu_size);
	    if (~cpu->flags & 1) continue;
	    if (!logical--) return i;
	  }
	  return 0;
	}
};
