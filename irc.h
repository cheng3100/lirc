#ifndef __IRC_H__
#define __IRC_H__

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#define perro(x) {fprintf(stderr, "%s:%d: %s: errno:%d %s\n", __FILE__, __LINE__, x, errno, strerror(errno));exit(1);}

enum tlv_type {
	MESSAGE,
	COMMAND,
};

enum cmd_opcode {
	HELP,
	PRIMODE,
	COMMODE,
	LIST,
	UNAME,
	RNAME,
	QUIT,
};

const char *op_cmd[] = {
[HELP]		"/help",
[PRIMODE]	"/msg",
[COMMODE]	"/com",
[LIST]		"/list",
[UNAME]		"/uname",
[RNAME]		"/rname",
[QUIT]		"/quit",
};

typedef struct {
	int t;
	int l;
	union {
		uint8_t u8;
		uint16_t u16;
		uint32_t u32;
		char	 *str;
		void	*tlv;
	} v;
} tlv_t;

enum tlv_value_type_e {
	V_U8,
	V_U16,
	V_U32,
	V_STR,
	V_BUF,
};


inline int tlv_serial_append(uint8_t *buf, int vtype, tlv_t *tlv ) {
	int offset= 0;

	buf[0] = tlv->t;
	buf[1] = tlv->l;
	offset +=2;
	if (vtype == V_STR) {
		memcpy(buf + offset, tlv->v.str, tlv->l);
		offset += tlv->l;
	} else if (vtype == V_U8) {
		buf[offset] = tlv->v.u8;
		offset += 1;
	} else if (vtype == V_U16) {
		uint16_t t16 = htons(tlv->v.u16);
		memcpy(buf+offset, &t16, 2);
		offset += 2;
	} else if (vtype == V_U32) {
		uint32_t t32 = htonl(tlv->v.u32);
		memcpy(buf+offset, &t32, 4);
		offset += 4;
	} else if (vtype == V_BUF) {
		// for next append
	}

	return offset;
}

inline int tlv_serial_append_str(uint8_t *buf_out, char *buf_in) {
	tlv_t t;
	t.t = MESSAGE;
	t.l = strlen(buf_in) + 1;   // include /n
	t.v.str = buf_in;

	tlv_serial_append(buf_out, V_STR, &t);
	
	return 2 + t.l;
}

inline int tlv_parse(uint8_t *buf, int vtype, tlv_t *tlv ) {
	int offset = 0;
	if (!tlv || !buf) {
		return -1;
	}

	tlv->t = buf[0];
	tlv->l = buf[1];
	offset +=2;

	if (vtype == V_STR) {
		tlv->v.str = (char *)(buf + offset);
		offset += tlv->l;
	} else if (vtype == V_U8) {
		tlv->v.u8 = buf[offset];
		offset += 1;
	} else if (vtype == V_U16) {
		tlv->v.u16 = ntohs(*(uint16_t *)(buf + offset));
		offset += 2;
	} else if (vtype == V_U32) {
		tlv->v.u32 = ntohl(*(uint32_t *)(buf + offset));
		offset += 4;
	} else if (vtype == V_BUF) {
		// for next append
		tlv->v.tlv = (void *)(buf + offset);
		offset += tlv->l;
	}

	return offset;
}

#endif
