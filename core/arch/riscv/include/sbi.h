/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright 2022 NXP
 */

#ifndef __SBI_H
#define __SBI_H

#if defined(CFG_RISCV_SBI)

/* SBI return error codes */
#define SBI_SUCCESS			 0
#define SBI_ERR_FAILURE			-1
#define SBI_ERR_NOT_SUPPORTED		-2
#define SBI_ERR_INVALID_PARAM		-3
#define SBI_ERR_DENIED			-4
#define SBI_ERR_INVALID_ADDRESS		-5
#define SBI_ERR_ALREADY_AVAILABLE	-6
#define SBI_ERR_ALREADY_STARTED		-7
#define SBI_ERR_ALREADY_STOPPED		-8

/* SBI Extension IDs */
#define SBI_EXT_0_1_CONSOLE_PUTCHAR	0x01
#define SBI_EXT_BASE			0x10
#define SBI_EXT_HSM			0x48534D
#define SBI_EXT_DBCN			0x4442434E
#define SBI_EXT_MPXY			0x4D505859

#ifndef __ASSEMBLER__

struct sbiret {
	long error;
	long value;
};

#define _sbi_ecall(ext, fid, arg0, arg1, arg2, arg3, arg4, arg5, ...) ({  \
	register unsigned long a0 asm("a0") = (unsigned long)arg0; \
	register unsigned long a1 asm("a1") = (unsigned long)arg1; \
	register unsigned long a2 asm("a2") = (unsigned long)arg2; \
	register unsigned long a3 asm("a3") = (unsigned long)arg3; \
	register unsigned long a4 asm("a4") = (unsigned long)arg4; \
	register unsigned long a5 asm("a5") = (unsigned long)arg5; \
	register unsigned long a6 asm("a6") = (unsigned long)fid;  \
	register unsigned long a7 asm("a7") = (unsigned long)ext;  \
	asm volatile ("ecall" \
		    : "+r" (a0), "+r" (a1) \
		    : "r" (a2), "r" (a3), "r" (a4), "r" (a5), "r"(a6), "r"(a7) \
		    : "memory"); \
	(struct sbiret){ .error = a0, .value = a1 }; \
})

#define sbi_ecall(...)	_sbi_ecall(__VA_ARGS__, 0, 0, 0, 0, 0, 0, 0)

/* SBI arguments */
struct sbi_args {
	unsigned long a0;
	unsigned long a1;
	unsigned long a2;
	unsigned long a3;
	unsigned long a4;
	unsigned long a5;
	unsigned long a6;
	unsigned long a7;
};

/* SBI function IDs for Base extension */
enum sbi_ext_base_fid {
	SBI_EXT_BASE_GET_SPEC_VERSION = 0,
	SBI_EXT_BASE_GET_IMP_ID,
	SBI_EXT_BASE_GET_IMP_VERSION,
	SBI_EXT_BASE_PROBE_EXT,
	SBI_EXT_BASE_GET_MVENDORID,
	SBI_EXT_BASE_GET_MARCHID,
	SBI_EXT_BASE_GET_MIMPID,
};

/* SBI function IDs for HSM extension */
enum sbi_ext_hsm_fid {
	SBI_EXT_HSM_HART_START = 0,
	SBI_EXT_HSM_HART_STOP,
	SBI_EXT_HSM_HART_GET_STATUS,
	SBI_EXT_HSM_HART_SUSPEND,
};

/* SBI function IDs for Debug Console extension */
enum sbi_ext_dbcn_fid {
	SBI_EXT_DBCN_CONSOLE_WRITE = 0,
	SBI_EXT_DBCN_CONSOLE_READ = 1,
	SBI_EXT_DBCN_CONSOLE_WRITE_BYTE = 2,
};

enum sbi_hsm_hart_state {
	SBI_HSM_STATE_STARTED = 0,
	SBI_HSM_STATE_STOPPED,
	SBI_HSM_STATE_START_PENDING,
	SBI_HSM_STATE_STOP_PENDING,
	SBI_HSM_STATE_SUSPENDED,
	SBI_HSM_STATE_SUSPEND_PENDING,
	SBI_HSM_STATE_RESUME_PENDING,
};

enum sbi_ext_mpxy_fid {
	SBI_EXT_MPXY_GET_SHMEM_SIZE,
	SBI_EXT_MPXY_SET_SHMEM,
	SBI_EXT_MPXY_GET_CHANNEL_IDS,
	SBI_EXT_MPXY_READ_ATTRS,
	SBI_EXT_MPXY_WRITE_ATTRS,
	SBI_EXT_MPXY_SEND_MSG_WITH_RESP,
	SBI_EXT_MPXY_SEND_MSG_WITHOUT_RESP,
	SBI_EXT_MPXY_GET_NOTIFICATION_EVENTS,
};

enum sbi_mpxy_attribute_id {
	/* Standard channel attributes managed by MPXY framework */
	SBI_MPXY_ATTR_MSG_PROT_ID		= 0x00000000,
	SBI_MPXY_ATTR_MSG_PROT_VER		= 0x00000001,
	SBI_MPXY_ATTR_MSG_MAX_LEN		= 0x00000002,
	SBI_MPXY_ATTR_MSG_SEND_TIMEOUT		= 0x00000003,
	SBI_MPXY_ATTR_MSG_COMPLETION_TIMEOUT	= 0x00000004,
	SBI_MPXY_ATTR_CHANNEL_CAPABILITY	= 0x00000005,
	SBI_MPXY_ATTR_SSE_EVENT_ID		= 0x00000006,
	SBI_MPXY_ATTR_MSI_CONTROL		= 0x00000007,
	SBI_MPXY_ATTR_MSI_ADDR_LO		= 0x00000008,
	SBI_MPXY_ATTR_MSI_ADDR_HI		= 0x00000009,
	SBI_MPXY_ATTR_MSI_DATA			= 0x0000000A,
	SBI_MPXY_ATTR_EVENTS_STATE_CONTROL	= 0x0000000B,
	SBI_MPXY_ATTR_STD_ATTR_MAX_IDX,
	/*
	 * Message protocol specific attributes, managed by
	 * the message protocol specification.
	 */
	SBI_MPXY_ATTR_MSGPROTO_ATTR_START	= 0x80000000,
	SBI_MPXY_ATTR_MSGPROTO_ATTR_END		= 0xffffffff
};

/**
 * SBI MPXY Message Protocol IDs
 */
enum sbi_mpxy_msgproto_id {
	SBI_MPXY_MSGPROTO_RPMI_ID = 0x0,
};

#include <compiler.h>
#include <encoding.h>
#include <stdint.h>
#include <sys/cdefs.h>
#include <types_ext.h>
#include <util.h>

int sbi_probe_extension(int extid);
void sbi_console_putchar(int ch);
int sbi_dbcn_write_byte(unsigned char ch);
int sbi_hsm_hart_start(uint32_t hartid, paddr_t start_addr, unsigned long arg);
int sbi_hsm_hart_get_status(uint32_t hartid, enum sbi_hsm_hart_state *status);
void thread_return_to_udomain_by_sbi_mpxy(unsigned long arg0,
					  unsigned long arg1,
					  unsigned long arg2,
					  unsigned long arg3,
					  unsigned long arg4,
					  unsigned long arg5 __unused);
void boot_primary_init_sbi_mpxy(void);
void boot_secondary_init_sbi_mpxy(void);

#endif /*__ASSEMBLER__*/
#endif /*defined(CFG_RISCV_SBI)*/
#endif /*__SBI_H*/
