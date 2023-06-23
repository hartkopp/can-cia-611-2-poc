/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cia-611-2.h - CAN CiA 611-2 definitions
 *
 */

#ifndef CIA_611_2_H
#define CIA_611_2_H

#include <linux/types.h>

#define DEFAULT_TRANSFER_ID 0x333 /* prio - undefined ? */
#define DEFAULT_VCID 0x0 /* undefined ? */
#define MPDU_SDT 0x08 /* to be confirmed in CiA 611-1 */
#define DEFAULT_AF 0x0 /* undefined ? */

/*
 * Multi-PDU contains C-PDU elements with a c_pdu_header structure.
 * Content in __u16 and __u32 is represented in network byte order.
 */
struct c_pdu_header {
	__u8 c_type; /* C-PDU protocol type, inspired by CiA 611-1 SDTs */
	__u8 c_info; /* C-PDU additional information (protocol type specific) */
	__u16 c_dlen; /* C-PDU data length (lower 11 bits) */
	__u32 c_id; /* C-PDU protocol type specific reference */
};

#define C_PDU_HEADER_SIZE (sizeof(struct c_pdu_header))
#define C_PDU_MIN_DATA_SIZE 4 /* at least 1 byte aligned to 4 byte */

#define MPDU_MIN_SIZE (C_PDU_HEADER_SIZE + C_PDU_MIN_DATA_SIZE)
#define MPDU_MAX_SIZE CANXL_MAX_DLEN
#define MPDU_DEFAULT_SIZE MPDU_MAX_SIZE

#endif /* CIA_611_2_H */
