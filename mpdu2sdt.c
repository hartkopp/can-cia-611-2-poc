/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * mpdu2sdt.c - CAN XL CiA 611-2 MPDU decomposer
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h> /* for network byte order conversion */

#include <linux/sockios.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include "cia-611-2.h"
#include "printframe.h"

extern int optind, opterr, optopt;

void print_usage(char *prg)
{
	fprintf(stderr, "%s - CAN XL CiA 611-2 MPDU decomposer\n\n", prg);
	fprintf(stderr, "Usage: %s [options] <src_if> <dst_if>\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -t <transfer_id> (TRANSFER ID "
		"- default: 0x%03X)\n", DEFAULT_TRANSFER_ID);
	fprintf(stderr, "         -l <size>        (limit PDU size"
		" to %ld .. %d, default: %d)\n", MPDU_MIN_SIZE, MPDU_MAX_SIZE,
		MPDU_DEFAULT_SIZE);
	fprintf(stderr, "         -v               (verbose)\n");
}

int main(int argc, char **argv)
{
	int opt;
	canid_t transfer_id = DEFAULT_TRANSFER_ID;
	int verbose = 0;

	int src, dst;
	struct sockaddr_can addr;
	struct can_filter rfilter;
	struct canxl_frame cfsrc, cfdst;
	struct c_pdu_header *c_pdu_hdr;
	unsigned int dataptr = 0;
	unsigned int padsz;

	int nbytes, ret;
	int sockopt = 1;
	struct timeval tv;

	while ((opt = getopt(argc, argv, "t:vh?")) != -1) {
		switch (opt) {

		case 't':
			transfer_id = strtoul(optarg, NULL, 16);
			if (transfer_id & ~CANXL_PRIO_MASK) {
				print_usage(basename(argv[0]));
				return 1;
			}
			break;

		case 'v':
			verbose = 1;
			break;

		case '?':
		case 'h':
		default:
			print_usage(basename(argv[0]));
			return 1;
			break;
		}
	}

	/* src_if and dst_if are two mandatory parameters */
	if (argc - optind != 2) {
		print_usage(basename(argv[0]));
		exit(0);
	}

	/* src_if */
	if (strlen(argv[optind]) >= IFNAMSIZ) {
		printf("Name of src CAN device '%s' is too long!\n\n",
		       argv[optind]);
		return 1;
	}

	/* dst_if */
	if (strlen(argv[optind + 1]) >= IFNAMSIZ) {
		printf("Name of dst CAN device '%s' is too long!\n\n",
		       argv[optind]);
		return 1;
	}

	/* open src socket */
	src = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (src < 0) {
		perror("src socket");
		return 1;
	}
	addr.can_family = AF_CAN;
	addr.can_ifindex = if_nametoindex(argv[optind]);

	/* enable CAN XL frames */
	ret = setsockopt(src, SOL_CAN_RAW, CAN_RAW_XL_FRAMES,
			 &sockopt, sizeof(sockopt));
	if (ret < 0) {
		perror("src sockopt CAN_RAW_XL_FRAMES");
		exit(1);
	}

	/* filter only for transfer_id (= prio_id) */
	rfilter.can_id = transfer_id;
	rfilter.can_mask = CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_SFF_MASK;
	ret = setsockopt(src, SOL_CAN_RAW, CAN_RAW_FILTER,
			 &rfilter, sizeof(rfilter));
	if (ret < 0) {
		perror("src sockopt CAN_RAW_FILTER");
		exit(1);
	}

	if (bind(src, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	/* open dst socket */
	dst = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (dst < 0) {
		perror("dst socket");
		return 1;
	}
	addr.can_family = AF_CAN;
	addr.can_ifindex = if_nametoindex(argv[optind + 1]);

	/* enable CAN XL frames */
	ret = setsockopt(dst, SOL_CAN_RAW, CAN_RAW_XL_FRAMES,
			 &sockopt, sizeof(sockopt));
	if (ret < 0) {
		perror("dst sockopt CAN_RAW_XL_FRAMES");
		exit(1);
	}

	if (bind(dst, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	/* main loop */
	while (1) {

		/* read source CAN XL frame */
		nbytes = read(src, &cfsrc, sizeof(struct canxl_frame));
		if (nbytes < 0) {
			perror("read");
			return 1;
		}

		if (nbytes < CANXL_HDR_SIZE + CANXL_MIN_DLEN) {
			fprintf(stderr, "read: no CAN frame\n");
			return 1;
		}

		if (!(cfsrc.flags & CANXL_XLF)) {
			fprintf(stderr, "read: no CAN XL frame flag\n");
			return 1;
		}

		if (nbytes != CANXL_HDR_SIZE + cfsrc.len) {
			printf("nbytes = %d\n", nbytes);
			fprintf(stderr, "read: no CAN XL frame len\n");
			return 1;
		}

		if (verbose) {
			if (ioctl(src, SIOCGSTAMP, &tv) < 0) {
				perror("SIOCGSTAMP");
				return 1;
			}

			/* print timestamp and device name */
			printf("\n(%ld.%06ld) %s ", tv.tv_sec, tv.tv_usec,
			       argv[optind]);

			printxlframe(&cfsrc);
		}

		if (cfsrc.sdt != MPDU_SDT) {
			printf("dropped received PDU as it is no M-PDU frame!");
                        continue;
		}

		/* size must be a padded length value */
		if (cfsrc.len % 4) {
			fprintf(stderr, "M-PDU not padded correctly (%d)\n",
				cfsrc.len);
			return 1;
		}

		/* size must be at least one C-PDU header and a padded byte */
		if (cfsrc.len < MPDU_MIN_SIZE) {
			fprintf(stderr, "M-PDU content too short (%d)\n",
				cfsrc.len);
			return 1;
		}

		/* start to decompose */
		dataptr = 0;

		while (1) {

			/* check for minimum length of C-PDU */
			if (dataptr > cfsrc.len - MPDU_MIN_SIZE)
				break;

			c_pdu_hdr = (struct c_pdu_header *) &cfsrc.data[dataptr];

			padsz = ntohs(c_pdu_hdr->c_dlen); /* get real data length */

			/* we have at least one data byte in a CAN XL frame */
			if (padsz < 1)
				break;

			/* need to round up to next 4 byte boundary? */
			if (padsz % 4)
				padsz += (4 - padsz % 4);

			/* does the C-PDU incl. data fit into the M-PDU space? */
			if (C_PDU_HEADER_SIZE + padsz > cfsrc.len - dataptr) {
				fprintf(stderr, "C-PDU content too long (%lu > %d)\n",
					C_PDU_HEADER_SIZE + padsz, cfsrc.len - dataptr);
				return 1;
			}

			/* create a valid STD frame from this C-PDU element */
			cfdst.prio = transfer_id;
			cfdst.flags = CANXL_XLF; /* no SEC bit */
			cfdst.sdt = c_pdu_hdr->c_type;
			cfdst.len = ntohs(c_pdu_hdr->c_dlen);
			cfdst.af = ntohl(c_pdu_hdr->c_id);

			dataptr += C_PDU_HEADER_SIZE;

			/* copy data - cfsrc.data is zero padded */
			memcpy(cfdst.data, &cfsrc.data[dataptr], padsz);

			dataptr += padsz;

			if (verbose) {
				printf("sending C-PDU ct %02X ci %02X dl %u id %08X psz %u dptr %u\n",
				       c_pdu_hdr->c_type, c_pdu_hdr->c_info,
				       ntohs(c_pdu_hdr->c_dlen),
				       ntohl(c_pdu_hdr->c_id), padsz, dataptr);
			}

			/* write C-PDU frame to destination socket */
			nbytes = write(dst, &cfdst, CANXL_HDR_SIZE + cfdst.len);
			if (nbytes != CANXL_HDR_SIZE + cfdst.len) {
				printf("nbytes = %d\n", nbytes);
				perror("write dst canxl_frame");
				exit(1);
			}

		} /* while (1) */

	} /* while (1) */

	close(src);
	close(dst);

	return 0;
}
