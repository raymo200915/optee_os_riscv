// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2024 Andes Technology Corporation
 */

#include <kernel/misc.h>
#include <kernel/panic.h>
#include <kernel/dt.h>
#include <libfdt.h>
#include <mm/core_memprot.h>
#include <mm/core_mmu.h>
#include <riscv.h>
#include <sbi.h>
#include <string.h>
#include <types_ext.h>
#include <util.h>

/** OPTEED MPXY Message IDs */
enum mpxy_opteed_message_id {
	OPTEED_MSG_COMMUNICATE = 0x01,
	OPTEED_MSG_COMPLETE = 0x02,
};

struct optee_msg_payload {
	unsigned long data[5];	/* a0~a4 */
};

struct sbi_mpxy {
	struct io_pa_va shmem_base;
	bool active;
};

static struct sbi_mpxy sbi_mpxy_hart_data[CFG_TEE_CORE_NB_CORE];

struct sbi_mpxy_opteed_ctx {
        uint32_t channel_id;
};

static struct sbi_mpxy_opteed_ctx mpxy_opteed_ctx = { 0 };


vaddr_t sbi_mpxy_get_shmem(void)
{
	uint32_t exceptions = thread_mask_exceptions(THREAD_EXCP_ALL);
	struct sbi_mpxy *mpxy = &sbi_mpxy_hart_data[get_core_pos()];
	thread_unmask_exceptions(exceptions);

	assert(mpxy->active);

	return mpxy->shmem_base.va;
}

int sbi_mpxy_setup_shmem(unsigned int hartid)
{
	struct sbiret ret = { };
	struct sbi_mpxy *mpxy = &sbi_mpxy_hart_data[hartid];
	void *shmem = NULL;

	if (mpxy->active) {
		return SBI_ERR_FAILURE;
	}

	/* Allocate 4 KiB memory aligend with 4 KiB (required by SBI MPXY). */
	shmem = memalign(SMALL_PAGE_SIZE, SMALL_PAGE_SIZE);
	if (!shmem) {
		EMSG("Allocate MPXY shared memory fail");
		return SBI_ERR_FAILURE;
	}

	mpxy->shmem_base.va = (vaddr_t)shmem;
	mpxy->shmem_base.pa = virt_to_phys(shmem);

	ret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_SET_SHMEM,
			SMALL_PAGE_SIZE, mpxy->shmem_base.pa, 0, 0);

	if (ret.error) {
		EMSG("Setup MPXY shared memory for hart%d error %ld",
		     hartid, ret.error);
		return SBI_ERR_FAILURE;
	}

	mpxy->active = true;

	EMSG("Setup MPXY shared memory for hart%d OK, PA: 0x%lX, VA: 0x%lX\n",
	     hartid, mpxy->shmem_base.pa, mpxy->shmem_base.va);

	return SBI_SUCCESS;
}

int sbi_mpxy_send_message_withresp(uint32_t channelid, uint32_t msgid,
				   void *tx, unsigned long tx_msglen,
				   void *rx, unsigned long *rx_msglen)
{
	struct sbiret ret = { };
	struct sbi_mpxy *mpxy = NULL;

	thread_mask_exceptions(THREAD_EXCP_ALL);
	mpxy = &sbi_mpxy_hart_data[get_core_pos()];

	if (tx_msglen) {
		memcpy((void *)mpxy->shmem_base.va, tx, tx_msglen);
	}

	ret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_SEND_MSG_WITH_RESP,
			channelid, msgid, tx_msglen, 0);

	if (!ret.error && rx) {
		memcpy(rx, (void *)mpxy->shmem_base.va, ret.value);
		if (rx_msglen) {
			*rx_msglen = ret.value;
		}
	}

	return ret.error;
}

void thread_return_to_udomain_by_mpxy(unsigned long arg0, unsigned long arg1,
				      unsigned long arg2, unsigned long arg3,
				      unsigned long arg4,
				      unsigned long arg5 __unused)
{
	struct optee_msg_payload optee_msg = {
		.data = {arg0, arg1, arg2, arg3, arg4},
	};

	sbi_mpxy_send_message_withresp(mpxy_opteed_ctx.channel_id,
				       OPTEED_MSG_COMPLETE,
				       &optee_msg, sizeof(optee_msg),
				       NULL, NULL);
}

void mpxy_opteed_channel_init(void)
{
	struct dt_descriptor *dt = get_external_dt_desc();
	void *fdt = dt->blob;
	const fdt32_t *p = NULL;
	int node = 0;
	int len = 0;

	if (!sbi_probe_extension(SBI_EXT_MPXY))
		panic("sbi mpxy extension must be supported");

	node = fdt_node_offset_by_compatible(fdt, -1,
					     "riscv,sbi-mpxy-opteed");
	if (node < 0)
		panic("\"sbi-mpxy-opteed\" node not found");

	p = fdt_getprop(fdt, node, "riscv,sbi-mpxy-channel-id", &len);
	if (!p)
		panic("\"riscv,sbi-mpxy-channel-id\" is not provided"
		      " in \"sbi-mpxy-opteed\" node");

	if (len != sizeof(*p))
		panic("\"riscv,sbi-mpxy-channel-id\" data size mismatch");

	mpxy_opteed_ctx.channel_id = fdt32_ld(p);

	/* TODO: sbi_mpxy_read_attrs to check if the channel supports
	 * TEE protocol
	 */

	DMSG("Registerd MPXY channel-%d with TEE protocol support",
	     mpxy_opteed_ctx.channel_id);
}
