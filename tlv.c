#include "irc.h"

int tlv_serial_append(uint8_t *buf, int v_type, tlv_t *tlv ) {

	buf[0] = tlv->t;
	buf[1] = tlv->l;
	memcpy(buf + 2, tlv->v.str, tlv->l);

	return tlv->l + 2;	// t\l is fixed to 2 byte
}

int tlv_parse(uint8_t *buf, int vtype, tlv_t *tlv ) {
	return 0;
}
