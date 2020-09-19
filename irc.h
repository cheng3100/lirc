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

//TODO:tlv fix
typedef struct {
	uint8_t  t;
	uint16_t l;
	uint8_t v[0];
} __attribute__((packed)) tlv_t;

enum tlv_value_type_e {
	V_U8,
	V_U16,
	V_U32,
	V_STR,
	V_TLV,
};


inline int tlv_serial_append(uint8_t *buf, tlv_t *tlv)
{
	memcpy(buf, (void *)tlv, tlv->l + 3);

	((tlv_t *)buf)->l = htons(tlv->l);

	return tlv->l + 3;
}

// inline int tlv_serial_append_str(uint8_t *buf_out, char *buf_in) {
//     tlv_t t;
//     t.t = MESSAGE;
//     t.l = strlen(buf_in) + 1;   [> include /0 <]
//     t.v = (void *)buf_in;
//
//     tlv_serial_append(buf_out, &t);
//
//     return 2 + t.l;
// }

inline int tlv_parse(uint8_t *buf, tlv_t **tlv)
{
	if (!tlv || !buf) {
		return -1;
	}

	*tlv = (tlv_t *)buf;
	(*tlv)->l = ntohs((*tlv)->l);

	return (*tlv)->l + 3;
}

#endif
