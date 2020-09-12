#include "irc.h"

#define PORT 5555
#define MAX_MSG_LENGTH 1024
#define END_STRING "chau\n"
#define COMPLETE_STRING "fin-respuesta"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL SO_NOSIGPIPE
#endif

#define perro(x) {fprintf(stderr, "%s:%d: %s: errno:%d %s\n", __FILE__, __LINE__, x, errno, strerror(errno));exit(1);}

void send_cmd(int sock, int pid) {
	char str[MAX_MSG_LENGTH] = {0};
	printf("> ");
	while (fgets(str, MAX_MSG_LENGTH, stdin) == str) {
		if(strncmp(str, END_STRING, strlen(END_STRING)) == 0) break;
		if(send(sock, str, strlen(str)+1, 0) < 0) perro("send");
	}
	kill(pid, SIGKILL);
	printf("Goodbye.\n");
}

void receive(int sock) {
	char buf[MAX_MSG_LENGTH] = {0};
	int filled = 0;
	while(filled = recv(sock, buf, MAX_MSG_LENGTH-1, 0)) {
		buf[filled] = '\0';
		printf("%s", buf);
		fflush(stdout);
	}
	int ppid = getppid();
	kill(ppid, SIGKILL);
	printf("Server disconnected.\n");
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
	/** connection.sin_port = htons(PORT); */
	connection.sin_port = htons(atoi(argv[2]));
	if (connect(sock, (const struct sockaddr*) &connection, sizeof(connection)) != 0) perro("connect");

	int pid;
	if(pid = fork()) send_cmd(sock, pid);
	else receive(sock);

	return 0;
}
