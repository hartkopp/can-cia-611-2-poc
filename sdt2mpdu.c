/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * std2mpdu.c - CAN XL CiA 611-2 MPDU composer
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
#include <sys/timerfd.h>
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
	fprintf(stderr, "%s - CAN XL CiA 611-2 MPDU composer\n\n", prg);
	fprintf(stderr, "Usage: %s [options] <src_if> <dst_if>\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -t <transfer_id> (TRANSFER ID "
		"- default: 0x%03X)\n", DEFAULT_TRANSFER_ID);
	fprintf(stderr, "         -l <size>        (limit PDU size"
		" to %ld .. %d, default: %d)\n", MPDU_MIN_SIZE, MPDU_MAX_SIZE,
		MPDU_DEFAULT_SIZE);
	fprintf(stderr, "         -v               (verbose)\n");
}

void write_mpdu(int s, struct canxl_frame *cfx, unsigned int *dataptr)
{
	int nbytes;

	cfx->len = *dataptr;

	/* paranoia check */
	if (!cfx->len) {
		printf("%s: failure: dataptr is zero!\n", __FUNCTION__);
		exit(1);
	}

	/* write M-PDU frame to destination socket */
	nbytes = write(s, cfx, CANXL_HDR_SIZE + cfx->len);
	if (nbytes != CANXL_HDR_SIZE + cfx->len) {
		printf("nbytes = %d\n", nbytes);
		perror("write dst canxl_frame");
		exit(1);
	}

	/* clear M-PDU frame */
	*dataptr = 0;
}

int main(int argc, char **argv)
{
	int opt;
	canid_t transfer_id = DEFAULT_TRANSFER_ID;
	unsigned int mpdu_max_size = MPDU_DEFAULT_SIZE;
	int verbose = 0;

	int src, dst; /* sockets */
	int tfd; /* timer fd */
	fd_set rdfs;

	struct sockaddr_can addr;
	struct can_filter rfilter;
	struct canxl_frame cfsrc, cfdst;
	struct c_pdu_header *c_pdu_hdr;
	unsigned int dataptr = 0;
	unsigned int padsz;

	int nbytes, ret;
	int sockopt = 1;
	struct timeval tv;

	struct itimerspec spec = {
		{ 0, 0 }, /* no interval timer */
		{ 0, 0 }  /* no single timeout */
	};

	while ((opt = getopt(argc, argv, "t:l:vh?")) != -1) {
		switch (opt) {

		case 't':
			transfer_id = strtoul(optarg, NULL, 16);
			if (transfer_id & ~CANXL_PRIO_MASK) {
				print_usage(basename(argv[0]));
				return 1;
			}
			break;

		case 'l':
			mpdu_max_size = strtoul(optarg, NULL, 10);
			if (mpdu_max_size < MPDU_MIN_SIZE ||
			    mpdu_max_size > MPDU_MAX_SIZE ||
			    mpdu_max_size % 4) {
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

	tfd = timerfd_create(CLOCK_MONOTONIC,  0);
	if (tfd < 0) {
		perror("timerfd create");
		return 1;
	}

	/* set defaults for M-PDU CAN XL frame */
	cfdst.prio = transfer_id;
	cfdst.flags = CANXL_XLF; /* no SEC bit */
	cfdst.sdt = MPDU_SDT;
	cfdst.af = DEFAULT_AF;

	/* main loop */
	while (1) {

		/* clear data for copying zero padded content */
		memset(cfsrc.data, 0, sizeof(cfsrc.data));

		FD_ZERO(&rdfs);
		FD_SET(src, &rdfs);
		FD_SET(tfd, &rdfs);

		ret = select(((src > tfd)?src:tfd) + 1, &rdfs, NULL, NULL, NULL);
		if (ret < 0) {
			perror("select");
			return 1;
		}

		if (FD_ISSET(tfd, &rdfs)) {
			/* stop timer */
			spec.it_value.tv_sec = 0;
			spec.it_value.tv_nsec = 0;
			timerfd_settime(tfd, 0, &spec, NULL);

			if (verbose)
				printf("(timeout) sending M-PDU with length %u\n", dataptr);

			write_mpdu(dst, &cfdst, &dataptr);
		}

		if (!FD_ISSET(src, &rdfs))
			continue;

		/* read CAN XL frame */
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
			printf("(%ld.%06ld) %s ", tv.tv_sec, tv.tv_usec,
			       argv[optind]);

			printxlframe(&cfsrc);
		}

		padsz = cfsrc.len; /* real data length - not the DLC */

		/* need to round up to next 4 byte boundary? */
		if (padsz % 4)
			padsz += (4 - padsz % 4);

		/* does the new PDU generally fit into the C-PDU space? */
		if (C_PDU_HEADER_SIZE + padsz > mpdu_max_size) {
			printf("dropped received PDU as it does not fit into M-PDU frame limit!");
			continue;
		}

		/* does the new PDU still fit into currently available M-PDU space? */
		if (C_PDU_HEADER_SIZE + padsz > mpdu_max_size - dataptr) {

			/* no => send out the current M-PDU to make space */

			if (verbose)
				printf("(buffer) sending M-PDU with length %u\n", dataptr);

			/* stop timer */
			spec.it_value.tv_sec = 0;
			spec.it_value.tv_nsec = 0;
			timerfd_settime(tfd, 0, &spec, NULL);

			write_mpdu(dst, &cfdst, &dataptr);
		}

		if (dataptr == 0) {
			/* start timer when adding the first C-PDU element */
			spec.it_value.tv_sec = 1;
			spec.it_value.tv_nsec = 0;
			timerfd_settime(tfd, 0, &spec, NULL);
		}

		/* fill C-PDU header */
		c_pdu_hdr = (struct c_pdu_header *) &cfdst.data[dataptr];

		c_pdu_hdr->c_type = cfsrc.sdt;
		c_pdu_hdr->c_info = DEFAULT_VCID;
		c_pdu_hdr->c_dlen = htons(cfsrc.len);
		c_pdu_hdr->c_id = htonl(cfsrc.af);

		dataptr += C_PDU_HEADER_SIZE;

		/* copy data - cfsrc.data is zero padded */
		memcpy(&cfdst.data[dataptr], cfsrc.data, padsz);

		dataptr += padsz;

		if (verbose) {
			printf("added C-PDU ct %02X ci %02X dl %u id %08X psz %u dptr %u\n",
			       c_pdu_hdr->c_type, c_pdu_hdr->c_info, c_pdu_hdr->c_dlen,
			       c_pdu_hdr->c_id, padsz, dataptr);
		}

	} /* while(1) */

	close(src);
	close(dst);

	return 0;
}
