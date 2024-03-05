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
	uint32_t channel_id;
	bool active;
};

static struct sbi_mpxy sbi_mpxy_hart_data[CFG_TEE_CORE_NB_CORE];

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
			mpxy->shmem_base.pa, 0, 0);

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

int sbi_mpxy_read_attributes(uint32_t channel_id, uint32_t base_attribute_id,
			     uint32_t attribute_count, void *attribute_buf)
{
	struct sbiret sret;
	struct sbi_mpxy *mpxy;
	uint32_t exceptions;

	if (!attribute_count || !attribute_buf)
		return SBI_ERR_INVALID_PARAM;

	exceptions = thread_mask_exceptions(THREAD_EXCP_ALL);
	mpxy = &sbi_mpxy_hart_data[get_core_pos()];
	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_READ_ATTRS,
			 channel_id, base_attribute_id, attribute_count,
			 0, 0, 0);
	if (!sret.error) {
		memcpy(attribute_buf, (void *)mpxy->shmem_base.va,
		       attribute_count * sizeof(uint32_t));
	}
	thread_unmask_exceptions(exceptions);

	return sret.error;
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

/* Only called from assembly */
void thread_prepare_return_to_udomain_by_mpxy(unsigned long arg0,
					      unsigned long arg1,
					      unsigned long arg2,
					      unsigned long arg3,
					      unsigned long arg4,
					      unsigned long arg5 __unused,
					      struct thread_mpxy_args *args)
{
	struct sbi_mpxy *mpxy = NULL;
	struct optee_msg_payload optee_msg = {
		.data = {arg0, arg1, arg2, arg3, arg4},
	};
	size_t pos;

	assert((thread_get_exceptions() & THREAD_EXCP_ALL) == THREAD_EXCP_ALL);
	pos = get_core_pos();
	mpxy = &sbi_mpxy_hart_data[pos];
	memcpy((void *)mpxy->shmem_base.va, &optee_msg, sizeof(optee_msg));

	args->a7 = SBI_EXT_MPXY;
	args->a6 = SBI_EXT_MPXY_SEND_MSG_WITH_RESP;
	args->a0 = sbi_mpxy_hart_data[pos].channel_id;
	args->a1 = OPTEED_MSG_COMPLETE;
	args->a2 = sizeof(optee_msg);
	args->a3 = 0;
	args->a4 = 0;
	args->a5 = 0;
}

void mpxy_opteed_init(void)
{
	struct dt_descriptor *dt = get_external_dt_desc();
	void *fdt = dt->blob;
	const fdt32_t *p = NULL;
	int ret, i, node, len;
	uint32_t prot_id;
	size_t pos;

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

	len = len / sizeof(fdt32_t);
	if (len != CFG_TEE_CORE_NB_CORE)
		panic("\"riscv,sbi-mpxy-channel-id\" not enough channels");
	for (i = 0; i < len; i++) {
		sbi_mpxy_hart_data[i].channel_id = fdt32_to_cpu(p[i]);
	}

	/* Setup MPXY share memory for primary hart */
	pos = get_core_pos();
	ret = sbi_mpxy_setup_shmem(pos);
	if (ret)
		panic("Failed to setup MPXY shared memory");

	/* Check channel protocol ID */
	for (i = 0; i < len; i++) {
		ret = sbi_mpxy_read_attributes(sbi_mpxy_hart_data[i].channel_id,
					       SBI_MPXY_ATTR_MSG_PROT_ID, 1,
					       &prot_id);
		if (ret) {
			EMSG("Read MPXY channel-%d protocol ID failed, ret=%d",
			     sbi_mpxy_hart_data[i].channel_id, ret);
			panic();
		}
		if (prot_id != SBI_MPXY_MSGPROTO_TEE_ID) {
			EMSG("MPXY channel-%d protocol ID %d not matched",
			     sbi_mpxy_hart_data[i].channel_id, prot_id);
			panic();
		}
		DMSG("Registerd MPXY channel-%d supports protocol ID %d",
		     sbi_mpxy_hart_data[i].channel_id, prot_id);
	}
}
