/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cia-611-2.h - CAN CiA 611-2 definitions
 *
 */

#ifndef CIA_611_2_H
#define CIA_611_2_H

#include <linux/types.h>

struct c_pdu_header {
	__u8 c_type; /* C-PDU protocol type, inspired by CiA 611-1 SDTs */
	__u8 c_info; /* C-PDU additional information (protocol type specific) */
	__u16 c_dlen; /* C-PDU data length (lower 11 bits) */
	__u32 c_id; /* C-PDU protocol type specific reference */
};

#define C_PDU_HEADER_SIZE (sizeof(struct c_pdu_header))

#endif /* CIA_611_2_H */
