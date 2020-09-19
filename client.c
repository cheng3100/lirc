#include "irc.h"
#include <semaphore.h>
#include <sys/mman.h>

#define MAX_MSG_LENGTH 254
#define MAX_PACKET_LENGTH 256

#define END_STRING "/end\n"
#define WORD_COUNT "/wc\n"

struct wcount_t {
	sem_t mutex;
	int   count;
} *wc;

int packet_constrcut(uint8_t *pbuf, uint8_t* mbuf, int mlen)
{
	int offset = 0;
	tlv_t *tlv;

	if (!(tlv = malloc(sizeof(tlv_t) + mlen)))
		perro("malloc fail");


	if (mbuf[0] == '/') {    // command
		tlv->t = COMMAND;
	} else {
		tlv->t = MESSAGE;
	}

	tlv->l = mlen;
	memcpy(tlv->v, mbuf, mlen);

	offset = tlv_serial_append(pbuf, tlv);

	free(tlv);

	return offset;
}

void send_cmd(int sock, int pid)
{
	char str[MAX_MSG_LENGTH] = {0};
	uint8_t packet[MAX_PACKET_LENGTH] = {0};
	uint32_t len;

	while (fgets(str, MAX_MSG_LENGTH, stdin) == str) {
		str[MAX_MSG_LENGTH-1] = '\0';
		/*
		 * stdout print format:
		 * 1. "\033[A" : move cursor up one line but in same column.
		 * 2. "\33[2K" : erases the entire line your cursor is currently on.
		 */
		printf("\033[A\33[2K\r<<< %s", str);
		/* local cmd */
		if(strncmp(str, END_STRING, strlen(END_STRING)) == 0)
			break;
		else if (strncmp(str, WORD_COUNT, strlen(WORD_COUNT)) == 0) {
			printf(">>> byte count is %d\n", wc->count);
			continue;
		}

		len = packet_constrcut(packet, (uint8_t*)str, strlen(str));
		if(send(sock, packet, len, 0) < 0) perro("send");

		sem_wait(&wc->mutex);
		wc->count += strlen(str);
		sem_post(&wc->mutex);
	}
	// TODO exit fix
	kill(pid, SIGKILL);
	printf("Goodbye.\n");
	exit(EXIT_SUCCESS);
}

void receive(int sock)
{
	uint8_t buf[MAX_MSG_LENGTH] = {0};
	int filled = 0, msg_byte=0;
	while((filled = recv(sock, buf, MAX_MSG_LENGTH-1, 0))) {
		/* reserve:server to client cmd */
		buf[filled] = '\0';
		printf("\33[2K\r>>> %s", buf);

		sem_wait(&wc->mutex);
		wc->count += msg_byte;
		sem_post(&wc->mutex);
	}
	int ppid = getppid();
	kill(ppid, SIGKILL);
	printf("Server disconnected.\n");
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	if(argc < 3 || argc > 3)
		perro("args");

	int sock = socket(PF_INET, SOCK_STREAM, 0);
	if(sock == -1) perro("socket");

	struct in_addr server_addr;
	if(!inet_pton(AF_INET, argv[1], &server_addr))
		perro("inet_pton");

	struct sockaddr_in connection;
	connection.sin_family = AF_INET;
	memcpy(&connection.sin_addr, &server_addr, sizeof(server_addr));
	// TODO
	/** memcpy_s(&connection.sin_addr, sizeof(connection.sin_addr), &server_addr, sizeof(server_addr)); */
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
