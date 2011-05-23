/*
 * Main sigma0 code.
 *
 * Copyright (C) 2008-2010, Bernhard Kauer <bk@vmmon.org>
 * Copyright (C) 2011, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
 * Copyright (C) 2011, Alexander Boettcher <boettcher@tudos.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of NUL (NOVA user land).
 *
 * NUL is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * NUL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

  void free_module(ModuleInfo * modinfo) {
    modinfo->mem = 0;
  }

  ModuleInfo * get_module(unsigned id) {
    if (id < 1 || id >= MAXMODULES) return 0;
    ModuleInfo *modinfo = _modinfo + id;
    return modinfo;
  }

  ModuleInfo * alloc_module(char const * cmdline, unsigned sigma0_cmdlen, bool s0_reserved = false) {
    // search for a free module
    again:
    unsigned module = s0_reserved ? 1 : 5;
    while (module < MAXMODULES && *reinterpret_cast<unsigned long volatile *>(&_modinfo[module].mem)) module++;
    if (module >= MAXMODULES) return 0;
    if (0 != Cpu::cmpxchg4b(&_modinfo[module].mem, 0, 1)) goto again;

    // init module parameters
    ModuleInfo *modinfo = _modinfo + module;
    assert(reinterpret_cast<unsigned long>(&modinfo->mem) + sizeof(modinfo->mem) == reinterpret_cast<unsigned long>(modinfo) + sizeof(*modinfo));
    memset(modinfo, 0, sizeof(*modinfo) - sizeof(modinfo->mem));

    modinfo->id             = module;
    char *p = strstr(cmdline, "sigma0::cpu");
    if (p > cmdline + sigma0_cmdlen) p = 0;
    modinfo->cpunr          = _cpunr[( p ? strtoul(p+12, 0, 0) : ++_last_affinity) % _numcpus];
    p = strstr(cmdline, "sigma0::dma");
    modinfo->dma            = p > cmdline + sigma0_cmdlen ? 0 : p;
    modinfo->cmdline        = cmdline;
    modinfo->sigma0_cmdlen  = sigma0_cmdlen;
    modinfo->type           = ModuleInfo::TYPE_APP;

    if (verbose & VERBOSE_INFO) {
      Logging::printf("s0: [%2u] module '", modinfo->id);
      fancy_output(cmdline, 4096);
    }

    return modinfo;
  }

  unsigned boot_s0_services(Utcb * utcb) {
    char const * file_name = "admission.nulconfig"; //XXX to do
    unsigned res, id;
    char * config = 0;

    FsProtocol fs_obj = FsProtocol(alloc_cap(FsProtocol::CAP_SERVER_PT + _hip->cpu_desc_count()), "fs/embedded");
    FsProtocol::dirent fileinfo;
    if (res = fs_obj.get_file_info(*utcb, fileinfo, file_name)) goto cleanup;

    if (!(config = new (0x1000U) char[fileinfo.size+1])) goto cleanup;
    res = fs_obj.get_file_copy(*utcb, config, fileinfo.size, file_name);
    config[fileinfo.size] = 0;

    if (res != ENONE) goto cleanup;
    res = start_config(utcb, config, id, true);

    cleanup:
    if (res != ENONE) {
      if (config) delete [] config;
    }
    fs_obj.destroy(*utcb, FsProtocol::CAP_SERVER_PT + _hip->cpu_desc_count(), this);
    return res;
  }

  /**
   * Starts a configuration loaded during boottime. Configuration numbers start at zero.
   */
  unsigned start_config(Utcb *utcb, unsigned which, unsigned &internal_id)
  {
    // Skip to interesting configuration
    char const *cmdline = 0;
    Hip_mem *mod;
    unsigned configs = 0;
    unsigned i, len;

    for (i=1; mod = __hip->get_mod(i); i++) {
      cmdline = reinterpret_cast<char const *>(mod->aux);
      len = strcspn(cmdline, " \t\r\n\f");
      if (!(len >= 10 && !strncmp(cmdline + len - 10, ".nulconfig", 10))) continue;
      if (configs++ == which) break;
    }
    if (!mod) {
      Logging::printf ("Cannot find configuration %u.\n", which);
      return __LINE__;
    }
    if (mod->size == 1) {
      if (!(len = strcspn(cmdline, " \t\r\n\f"))) return __LINE__;
      cmdline += len;
    } else {
      char endc;
      cmdline = reinterpret_cast<char const *>(mod->addr);
      if ((mod->size > 0) && ((endc = *(cmdline + mod->size - 1)) != 0)) {
        if (endc > 32) {
          Logging::printf("s0: config %u has invalid end character (got %x : expected 0 - 32)\n", which, endc);
          return __LINE__;
        }
        *(reinterpret_cast<char *>(mod->addr) + mod->size - 1) = 0;
      }
    }
    return start_config(utcb, cmdline, internal_id);
  }

  /**
   * Start a configuration from a stable memory region (mconfig). Region has to be zero terminated.
   */
  unsigned start_config(Utcb *utcb, char const * mconfig, unsigned &internal_id, bool part_of_s0 = false)
  {
    char const * file_name, * client_cmdline;

    if (!mconfig || (!(client_cmdline = strstr(mconfig, "||")))) return __LINE__;
    unsigned long sigma0_cmdlen = client_cmdline - mconfig;
    client_cmdline += 2 + strspn(client_cmdline + 2, " \t\r\n\f");
    if (!(file_name = strstr(client_cmdline, "://")) || client_cmdline + strcspn(client_cmdline, " \t\r\n\f") < file_name) return __LINE__;

    if (file_name - client_cmdline > 64 - 4) return __LINE__;
    char fs_name [64] = "fs/";
    memcpy(&fs_name[3], client_cmdline, file_name - client_cmdline);
    fs_name[file_name - client_cmdline + 3] = 0;

    file_name += 3;
    unsigned namelen = strcspn(file_name, " \t\r\n\f");
    unsigned res;
    unsigned long long msize, physaddr;
    char *addr;
    ModuleInfo * modinfo;

    FsProtocol fs_obj = FsProtocol(alloc_cap(FsProtocol::CAP_SERVER_PT + _hip->cpu_desc_count()), fs_name);
    FsProtocol::dirent fileinfo;
    if (fs_obj.get_file_info(*utcb, fileinfo, file_name, namelen)) { Logging::printf("s0: File not found '%s'\n", file_name); res = __LINE__; goto fs_out; }

    msize = (fileinfo.size + 0xfff) & ~0xffful;
    {
      SemaphoreGuard l(_lock_mem);
      physaddr = _free_phys.alloc(msize, 12);
    }
    if (!physaddr || !msize) { Logging::printf("s0: Not enough memory\n"); res = __LINE__; goto fs_out; }

    addr = map_self(utcb, physaddr, msize);
    if (!addr) { Logging::printf("s0: Could not map file\n"); res= __LINE__; goto phys_out; }
    if (fs_obj.get_file_copy(*utcb, addr, fileinfo.size, file_name, namelen)) { Logging::printf("s0: Getting file failed %s.\n", file_name); res = __LINE__; goto map_out; }

    modinfo = alloc_module(mconfig, sigma0_cmdlen, part_of_s0);
    if (!modinfo) { Logging::printf("s0: to many modules to start -- increase MAXMODULES in %s\n", __FILE__); res = __LINE__; goto map_out; }
    if (admission && modinfo->id == 1) modinfo->type = ModuleInfo::TYPE_ADMISSION; //XXX

    res = _start_config(utcb, addr, fileinfo.size, client_cmdline, modinfo);

    if (!res) internal_id = modinfo->id;
    if (res) free_module(modinfo);

  map_out:
    //don't try to unmap from ourself "revoke(..., true)"
    //map_self may return a already mapped page (backed by 4M) which contains the requested phys. page
    //revoking a small junk of a larger one unmaps the whole area ...
    revoke_all_mem(reinterpret_cast<void *>(addr), msize, DESC_MEM_ALL, false);
  phys_out:
    {
      SemaphoreGuard l(_lock_mem);
      _free_phys.add(Region(physaddr, msize));
    }
  fs_out:
    fs_obj.destroy(*utcb, FsProtocol::CAP_SERVER_PT + _hip->cpu_desc_count(), this);

    return res;
  }

  unsigned _start_config(Utcb * utcb, char * elf, unsigned long mod_size,
                         char const * client_cmdline, ModuleInfo * modinfo)
  {
    AdmissionProtocol::sched sched; //Qpd(1, 100000)
    unsigned res = 0, slen, pt = 0;
    unsigned long pmem = 0, maxptr = 0;
    Hip * modhip = 0;
    char * tmem;

    // Figure out how much memory we give the client. If the user
    // didn't give a limit, we give it enough to unpack the ELF.
    unsigned long psize_needed = elf ? Elf::loaded_memsize(elf, mod_size) : ~0u;
    char *p = strstr(modinfo->cmdline, "sigma0::mem:");
    if (p > modinfo->cmdline + modinfo->sigma0_cmdlen) p = NULL;
    modinfo->physsize = (p == NULL) ? psize_needed : (strtoul(p+sizeof("sigma0::mem:")-1, 0, 0) << 20);

    // Round up to page size
    modinfo->physsize = (modinfo->physsize + 0xFFF) & ~0xFFF;

    check1(__LINE__, (modinfo->physsize > (CLIENT_BOOT_UTCB - MEM_OFFSET)),
           "s0: [%2u] Cannot allocate more than %u KB for client, requested %lu KB.\n", modinfo->id,
           CLIENT_BOOT_UTCB - MEM_OFFSET, modinfo->physsize / 1024);

    check1(__LINE__, ((psize_needed > modinfo->physsize) || !elf),
           "s0: [%2u] Could not allocate %ld MB memory. We need %ld MB.\n", modinfo->id, modinfo->physsize >> 20, psize_needed >> 20);

    {
      SemaphoreGuard l(_lock_mem);
      if (!(pmem = _free_phys.alloc(modinfo->physsize, 22))) {
        _free_phys.debug_dump("free phys");
        _virt_phys.debug_dump("virt phys");
        _free_virt.debug_dump("free virt");
      }
      check1(__LINE__, !pmem);
    }

    check2(_free_pmem, (!((tmem = map_self(utcb, pmem, modinfo->physsize))))); //don't assign modinfo->mem = map_self directly (double free may happen)
    modinfo->mem = tmem;

    if (verbose & VERBOSE_INFO)
      Logging::printf("s0: [%2u] using memory: %ld MB (%lx) at %lx\n", modinfo->id, modinfo->physsize >> 20, modinfo->physsize, pmem);

    /**
     * We memset the client memory to make sure we get an
     * deterministic run and not leak any information between
     * clients.
     */
    memset(modinfo->mem, 0, modinfo->physsize);

    // decode ELF
    check2(_free_pmem, (Elf::decode_elf(elf, mod_size, modinfo->mem, modinfo->rip, maxptr, modinfo->physsize, MEM_OFFSET, Config::NUL_VERSION)));

    modinfo->hip = new (0x1000) char[0x1000];

    // allocate a console for it
    alloc_console(modinfo, modinfo->cmdline);
    attach_drives(modinfo->cmdline, modinfo->sigma0_cmdlen, modinfo->id);

    // create a HIP for the client
    slen = strlen(client_cmdline) + 1;
    check2(_free_caps, (slen + _hip->length + 2*sizeof(Hip_mem) > 0x1000), "s0: configuration to large\n");

    modhip = reinterpret_cast<Hip *>(modinfo->hip);
    memcpy(reinterpret_cast<char *>(modhip) + 0x1000 - slen, client_cmdline, slen);

    memcpy(modhip, _hip, _hip->mem_offs);
    modhip->length = modhip->mem_offs;
    modhip->append_mem(MEM_OFFSET, modinfo->physsize, 1, pmem);
    modhip->append_mem(0, slen, -2, CLIENT_HIP + 0x1000 - slen);
    modhip->fix_checksum();
    assert(_hip->length > modhip->length);

    // create special portal for every module
    pt = CLIENT_PT_OFFSET + (modinfo->id << CLIENT_PT_SHIFT);
    assert(_percpu[modinfo->cpunr].cap_ec_worker);
    check2(_free_caps, nova_create_pt(pt + 14, _percpu[modinfo->cpunr].cap_ec_worker, reinterpret_cast<unsigned long>(do_request_wrapper), MTD_RIP_LEN | MTD_QUAL));
    check2(_free_caps, nova_create_pt(pt + 30, _percpu[modinfo->cpunr].cap_ec_worker, reinterpret_cast<unsigned long>(do_startup_wrapper), 0));

    // create parent portals
    check2(_free_caps, service_parent->create_pt_per_client(pt, this));
    check2(_free_caps, nova_create_sm(pt + ParentProtocol::CAP_PARENT_ID));

    // map idle SCs, so that they can be mapped to the admission service during create_pd
    if (admission && modinfo->type == ModuleInfo::TYPE_ADMISSION) {
      utcb->add_frame();
      *utcb << Crd(0, 31, DESC_CAP_ALL);
      for (unsigned sci=0; sci < _hip->cpu_desc_count(); sci++) {
        Hip_cpu const *cpu = &_hip->cpus()[sci];
        if (not cpu->enabled()) continue;
        Utcb::TypedMapCap(0 + sci).fill_words(utcb->msg + utcb->head.untyped, Crd(pt + ParentProtocol::CAP_PT_PERCPU + MAXCPUS + sci, 0, MAP_HBIT).value());
        utcb->head.untyped += 2;
      }

      check2(_free_caps, nova_call(_percpu[utcb->head.nul_cpunr].cap_pt_echo));

      for (unsigned sci=0; sci < _hip->cpu_desc_count(); sci++) { //sanity check that we really got the idle scs
        timevalue computetime;
        Hip_cpu const *cpu = &_hip->cpus()[sci];
        if (not cpu->enabled()) continue;
        unsigned char res = nova_ctl_sc(pt + ParentProtocol::CAP_PT_PERCPU + MAXCPUS + sci, computetime);
        assert(res == NOVA_ESUCCESS);
      }

      utcb->drop_frame();
    }

    if (verbose & VERBOSE_INFO)
      Logging::printf("s0: [%2u] creating PD%s on CPU %d\n", modinfo->id, modinfo->dma ? " with DMA" : "", modinfo->cpunr);

    check2(_free_caps, nova_create_pd(pt + NOVA_DEFAULT_PD_CAP, Crd(pt, CLIENT_PT_SHIFT,
           DESC_CAP_ALL & ((!admission || (admission && modinfo->type == ModuleInfo::TYPE_ADMISSION)) ? ~0U : ~(DESC_RIGHT_SC | DESC_RIGHT_PD)))));
    check2(_free_caps, nova_create_ec(pt + ParentProtocol::CAP_CHILD_EC,
			     reinterpret_cast<void *>(CLIENT_BOOT_UTCB), 0U,
			     modinfo->cpunr, 0, false, pt + NOVA_DEFAULT_PD_CAP));

    if (!admission || (admission && modinfo->type == ModuleInfo::TYPE_ADMISSION)) {
      check2(_free_caps, service_admission->alloc_sc(*utcb, pt + ParentProtocol::CAP_CHILD_EC, sched, modinfo->cpunr, this, "main", true));
    } else {
      //map temporary child id
      utcb->add_frame();
      *utcb << Crd(0, 31, DESC_CAP_ALL);
      Utcb::TypedMapCap(pt + ParentProtocol::CAP_PARENT_ID).fill_words(utcb->msg, Crd(pt + ParentProtocol::CAP_CHILD_ID, 0, MAP_MAP).value());
      utcb->head.untyped += 2;
      check2(_free_caps, nova_call(_percpu[utcb->head.nul_cpunr].cap_pt_echo));
      utcb->drop_frame();

      //allocate sc
      unsigned cap_base = alloc_cap(AdmissionProtocol::CAP_SERVER_PT + _hip->cpu_desc_count());
      AdmissionProtocol * s_ad = new AdmissionProtocol(cap_base, false); //don't block sigma0 when admission service is not available
      res = s_ad->get_pseudonym(*utcb, pt + ParentProtocol::CAP_CHILD_ID);
      if (!res) res = s_ad->alloc_sc(*utcb, pt + ParentProtocol::CAP_CHILD_EC, sched, modinfo->cpunr, "main");
      if (!res) s_ad->set_name(*utcb, "x"); 

      s_ad->close(*utcb, AdmissionProtocol::CAP_SERVER_PT + _hip->cpu_desc_count(), true, false);
      delete s_ad;

      unsigned char res = nova_revoke(Crd(pt + ParentProtocol::CAP_CHILD_ID, 0, DESC_CAP_ALL), true); //revoke tmp child id
      assert(!res);
      dealloc_cap(cap_base, AdmissionProtocol::CAP_SERVER_PT + _hip->cpu_desc_count());
    }

    _free_caps:
    if (res) {
      delete [] reinterpret_cast<char *>(modhip);

      // unmap all caps
      if (NOVA_ESUCCESS != nova_revoke(Crd(pt, CLIENT_PT_SHIFT, DESC_CAP_ALL), true))
        Logging::printf("s0: curiosity - nova_revoke failed\n");
    }

    _free_pmem:
    if (res) {
      SemaphoreGuard l(_lock_mem);
      _free_phys.add(Region(pmem, modinfo->physsize));
    }

    return res;
  }

  /**
   * Kill the given module.
   */
  unsigned kill_module(ModuleInfo * modinfo) {
    if (!modinfo || !_modinfo[modinfo->id].mem || !_modinfo[modinfo->id].physsize || modinfo->id < 5) return __LINE__;

    if (verbose & VERBOSE_INFO) Logging::printf("s0: [%2u] - initiate destruction of client ... \n", modinfo->id);
    // send kill message to parent service so that client specific data structures within a service can be released
    unsigned err = 0;
    unsigned recv_cap = alloc_cap();
    unsigned map_cap = CLIENT_PT_OFFSET + (modinfo->id << CLIENT_PT_SHIFT) + ParentProtocol::CAP_PARENT_ID;

    if (recv_cap) {
      Utcb &utcb = myutcb()->add_frame();
      Utcb::TypedMapCap(map_cap).fill_words(utcb.msg);
      utcb << Crd(recv_cap, 0, DESC_CAP_ALL);
      utcb.set_header(2, 0);
      if ((err = nova_call(_percpu[myutcb()->head.nul_cpunr].cap_pt_echo))) Logging::printf("s0: [%2u]   couldn't establish mapping %u\n", modinfo->id, err);
      utcb.drop_frame();

      if (!err) {
        utcb = myutcb()->add_frame();
        ParentProtocol::kill(utcb, recv_cap);
        dealloc_cap(recv_cap);
        utcb.drop_frame();
      }
    } else err = !recv_cap;
    if (err) Logging::printf("s0: [%2u]   can not inform service about dying client\n", modinfo->id);

    // unmap all service portals
    if (verbose & VERBOSE_INFO) Logging::printf("s0: [%2u]   revoke all caps\n", modinfo->id);
    unsigned res = nova_revoke(Crd(CLIENT_PT_OFFSET + (modinfo->id << CLIENT_PT_SHIFT), CLIENT_PT_SHIFT, DESC_CAP_ALL), true);
    if (res != NOVA_ESUCCESS) Logging::printf("s0: curiosity - nova_revoke failed %x\n", res);

    // and the memory
    if (verbose & VERBOSE_INFO) Logging::printf("s0: [%2u]   revoke all memory\n", modinfo->id);
    revoke_all_mem(modinfo->mem, modinfo->physsize, DESC_MEM_ALL, false);

    // change the tag
    Vprintf::snprintf(_console_data[modinfo->id].tag, sizeof(_console_data[modinfo->id].tag), "DEAD - CPU(%x) MEM(%ld)", modinfo->cpunr, modinfo->physsize >> 20);
    // switch to view 0 so that you can see the changed tag
    switch_view(global_mb, 0, _console_data[modinfo->id].console);

    // free resources
    SemaphoreGuard l(_lock_mem);
    Region * r = _virt_phys.find(reinterpret_cast<unsigned long>(modinfo->mem));
    assert(r);
    assert(r->size >= modinfo->physsize);
    _free_phys.add(Region(r->phys, modinfo->physsize));
    modinfo->physsize = 0;

    // XXX legacy - should be a service - freeing producer/consumer stuff
    unsigned cap;
    if (cap = _prod_network[modinfo->id].sm()) {
      if (verbose & VERBOSE_INFO) Logging::printf("s0: [%2u]   detach network\n", modinfo->id);
      dealloc_cap(cap);
      memset(&_prod_network[modinfo->id], 0, sizeof(_prod_network[modinfo->id]));
    }
    if (cap = _disk_data[modinfo->id].prod_disk.sm()) {
      if (verbose & VERBOSE_INFO) Logging::printf("s0: [%2u]   detach disks\n", modinfo->id);
      dealloc_cap(cap);
      memset(&_disk_data[modinfo->id], 0, sizeof(_disk_data[modinfo->id]));
    }
    if (cap = _console_data[modinfo->id].prod_stdin.sm()) {
      if (verbose & VERBOSE_INFO) Logging::printf("s0: [%2u]   detach stdin\n", modinfo->id);
      dealloc_cap(cap);
      //DEAD message dissappear here if you free _console_data ...
      //memset(&_console_data[modinfo->id], 0, sizeof(_console_data[modinfo->id]));
    }

    // XXX free more, such as GSIs, IRQs, Console...

    // XXX mark module as free -> we can not do this currently as we can not free all the resources
    if (verbose & VERBOSE_INFO) Logging::printf("s0: [%2u] - destruction done\n", modinfo->id);
    //free_module(modinfo);
    return 0;
  }
