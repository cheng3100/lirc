#include "irc.h"
#include <semaphore.h>
#include <sys/mman.h>

#define MAX_MSG_LENGTH 254
#define MAX_PACKET_LENGTH 256

#define END_STRING "/end\n"
#define WORD_COUNT "/wc\n"

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
		uint8_t	 *str;
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

struct wcount_t {
	sem_t mutex;
	int   count;
} *wc;

int tlv_serial_append(uint8_t *buf, int v_type, tlv_t *tlv ) {

	buf[0] = tlv->t;
	buf[1] = tlv->l;
	memcpy(buf + 2, tlv->v.str, tlv->l);

	return tlv->l + 2;	// t\l is fixed to 2 byte
}

int tlv_parse(uint8_t *buf, int vtype, tlv_t *tlv ) {
	return 0;
}

int packet_constrcut(uint8_t *pbuf, uint8_t* mbuf, int mlen) {
	int offset = 0;
	tlv_t tlv;
	if (mbuf[0] == '/') {    // command
		tlv.t = COMMAND;
	} else {
		tlv.t = MESSAGE;
	}

	tlv.l = mlen;
	tlv.v.str = mbuf;

	offset = tlv_serial_append(pbuf, V_STR, &tlv);

	return offset;
}

void send_cmd(int sock, int pid) {
	char str[MAX_MSG_LENGTH] = {0};
	uint8_t packet[MAX_PACKET_LENGTH] = {0};
	uint32_t len;

	while (fgets(str, MAX_MSG_LENGTH, stdin) == str) {
		/** stdout print format: */
		/**         1. "\033[A" : move cursor up one line but in same column. */
		/**         2. "\33[2K" : erases the entire line your cursor is currently on. */
		printf("\033[A\33[2K\r<<< %s", str);
		/** local cmd */
		if(strncmp(str, END_STRING, strlen(END_STRING)) == 0)
			break;
		else if (strncmp(str, WORD_COUNT, strlen(WORD_COUNT)) == 0) {
			printf(">>> byte count is %d\n", wc->count);
			continue;
		}

		len = packet_constrcut(packet, (uint8_t*)str, strlen(str));
		/** if(send(sock, str, strlen(str)+1, 0) < 0) perro("send"); */
		if(send(sock, packet, len, 0) < 0) perro("send");

		sem_wait(&wc->mutex);
		wc->count += strlen(str);
		sem_post(&wc->mutex);
	}
	kill(pid, SIGKILL);
	printf("Goodbye.\n");
	exit(EXIT_SUCCESS);
}

void receive(int sock) {
	uint8_t buf[MAX_MSG_LENGTH] = {0};
	int filled = 0, msg_byte=0;
	while((filled = recv(sock, buf, MAX_MSG_LENGTH-1, 0))) {
		buf[filled] = '\0';
		tlv_t tlv;
		tlv_parse(buf, V_STR, &tlv);
		switch (tlv.t) {
			case MESSAGE :
				printf("\33[2K\r>>> %s", tlv.v.str);
				msg_byte = tlv.l;
				break;
			case COMMAND:  break;  //reserve
			default: break;   // reserve
		}

		sem_wait(&wc->mutex);
		wc->count += msg_byte;
		sem_post(&wc->mutex);
	}
	int ppid = getppid();
	kill(ppid, SIGKILL);
	printf("Server disconnected.\n");
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
	if(argc < 2 || argc > 3) perro("args");

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1) perro("socket");

	struct in_addr server_addr;
	if(!inet_pton(AF_INET, argv[1], &server_addr)) perro("inet_pton");

	struct sockaddr_in connection;
	connection.sin_family = AF_INET;
	memcpy(&connection.sin_addr, &server_addr, sizeof(server_addr));
	connection.sin_port = htons(atoi(argv[2]));
	if (connect(sock, (const struct sockaddr*) &connection, sizeof(connection)) != 0) perro("connect");

	int pid;

	wc = mmap(NULL, sizeof(struct wcount_t), PROT_READ | PROT_WRITE, \
							MAP_SHARED | MAP_ANON, -1, 0);
	sem_init(&wc->mutex, 1, 1);

	if((pid = fork()))
		send_cmd(sock, pid);
	else
		receive(sock);

	return EXIT_SUCCESS;
}
