// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2023 Andes Technology Corporation
 * Copyright 2022-2023 NXP
 */

#include <assert.h>
#include <compiler.h>
#include <config.h>
#include <console.h>
#include <keep.h>
#include <kernel/boot.h>
#include <kernel/dt.h>
#include <kernel/linker.h>
#include <kernel/misc.h>
#include <kernel/panic.h>
#include <kernel/thread.h>
#include <libfdt.h>
#include <mm/core_memprot.h>
#include <mm/core_mmu.h>
#include <mm/tee_mm.h>
#include <mm/tee_pager.h>
#include <platform_config.h>
#include <riscv.h>
#include <sbi.h>
#include <stdio.h>
#include <trace.h>
#include <util.h>

#define PADDR_INVALID               ULONG_MAX

paddr_t start_addr;
unsigned long boot_args[4];

uint32_t sem_cpu_sync[CFG_TEE_CORE_NB_CORE];

#if defined(CFG_DT)
static int add_optee_dt_node(struct dt_descriptor *dt)
{
	int offs;
	int ret;

	if (fdt_path_offset(dt->blob, "/firmware/optee") >= 0) {
		DMSG("OP-TEE Device Tree node already exists!");
		return 0;
	}

	offs = fdt_path_offset(dt->blob, "/firmware");
	if (offs < 0) {
		offs = add_dt_path_subnode(dt, "/", "firmware");
		if (offs < 0)
			return -1;
	}

	offs = fdt_add_subnode(dt->blob, offs, "optee");
	if (offs < 0)
		return -1;

	ret = fdt_setprop_string(dt->blob, offs, "compatible",
				 "linaro,optee-tz");
	if (ret < 0)
		return -1;
	ret = fdt_setprop_string(dt->blob, offs, "method", "smc");
	if (ret < 0)
		return -1;

	return 0;
}

static int mark_tddram_as_reserved(struct dt_descriptor *dt)
{
	return add_res_mem_dt_node(dt, "optee_core", CFG_TDDRAM_START,
				   CFG_TDDRAM_SIZE);
}

static void update_external_dt(void)
{
	struct dt_descriptor *dt = get_external_dt_desc();

	if (!dt || !dt->blob)
		return;

	if (add_optee_dt_node(dt))
		panic("Failed to add OP-TEE Device Tree node");

#ifdef CFG_CORE_RESERVED_SHM
	if (mark_static_shm_as_reserved(dt))
		panic("Failed to config non-secure memory");
#endif

	if (mark_tddram_as_reserved(dt))
		panic("Failed to config secure memory");
}
#else /*CFG_DT*/
static void update_external_dt(void)
{
}
#endif /*!CFG_DT*/

void init_sec_mon(unsigned long nsec_entry __maybe_unused)
{
	assert(nsec_entry == PADDR_INVALID);
	/* Do nothing as we don't have a secure monitor */
}

#ifdef CFG_RISCV_S_MODE
static void start_secondary_cores(void)
{
	size_t i = 0;
	size_t pos = get_core_pos();

	for (i = 0; i < CFG_TEE_CORE_NB_CORE; i++)
		if (i != pos && IS_ENABLED(CFG_RISCV_SBI) &&
		    sbi_hsm_hart_start(i, start_addr, i))
			EMSG("Error starting secondary hart %zu", i);
}
#endif

static void init_runtime(void)
{
	malloc_add_pool(__heap1_start, __heap1_end - __heap1_start);

	IMSG_RAW("\n");
}

void init_tee_runtime(void)
{
	core_mmu_init_ta_ram();
	call_preinitcalls();
	call_initcalls();
}

static void init_primary(unsigned long nsec_entry)
{
	thread_init_core_local_stacks();

	/*
	 * Mask asynchronous exceptions before switch to the thread vector
	 * as the thread handler requires those to be masked while
	 * executing with the temporary stack. The thread subsystem also
	 * asserts that the foreign interrupts are blocked when using most of
	 * its functions.
	 */
	thread_set_exceptions(THREAD_EXCP_ALL);

	init_runtime();
	thread_init_boot_thread();
	thread_init_primary();
	thread_init_per_cpu();
	init_sec_mon(nsec_entry);
}

/* May be overridden in plat-$(PLATFORM)/main.c */
__weak void plat_primary_init_early(void)
{
}

/* May be overridden in plat-$(PLATFORM)/main.c */
__weak void boot_primary_init_intc(void)
{
}

/* May be overridden in plat-$(PLATFORM)/main.c */
__weak void boot_secondary_init_intc(void)
{
}

void boot_init_primary_early(void)
{
	unsigned long e = PADDR_INVALID;

	init_primary(e);
}

void boot_init_primary_late(unsigned long fdt,
			    unsigned long tos_fw_config __unused)
{
	int rc = 0;

	init_external_dt(fdt, CFG_DTB_MAX_SIZE);
	discover_nsec_memory();
	update_external_dt();

#ifdef CFG_RISCV_S_MODE
	mpxy_opteed_channel_init();
	rc = sbi_mpxy_setup_shmem(get_core_pos());
	if (rc)
		panic("Failed to setup MPXY shared memory");
#endif

	IMSG("OP-TEE version: %s", core_v_str);
	if (IS_ENABLED(CFG_INSECURE)) {
		IMSG("WARNING: This OP-TEE configuration might be insecure!");
		IMSG("WARNING: Please check https://optee.readthedocs.io/en/latest/architecture/porting_guidelines.html");
	}
	IMSG("Primary CPU initializing");
	boot_primary_init_intc();
	init_tee_runtime();
	call_finalcalls();
	IMSG("Primary CPU initialized");

#ifdef CFG_RISCV_S_MODE
	start_secondary_cores();
#endif
}

static void init_secondary_helper(unsigned long nsec_entry)
{
	size_t pos = get_core_pos();
	int rc = 0;

	IMSG("Secondary CPU %zu initializing", pos);

	/*
	 * Mask asynchronous exceptions before switch to the thread vector
	 * as the thread handler requires those to be masked while
	 * executing with the temporary stack. The thread subsystem also
	 * asserts that the foreign interrupts are blocked when using most of
	 * its functions.
	 */
	thread_set_exceptions(THREAD_EXCP_ALL);

	thread_init_per_cpu();
	init_sec_mon(nsec_entry);
	boot_secondary_init_intc();
	rc = sbi_mpxy_setup_shmem(get_core_pos());
	if (rc)
		panic("Failed to setup MPXY shared memory");
	IMSG("Secondary CPU %zu initialized", pos);
}

void boot_init_secondary(unsigned long nsec_entry __unused)
{
	init_secondary_helper(PADDR_INVALID);
}
