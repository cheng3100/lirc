#include "irc.h"

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048

#define INV_UID 0		// reserve for invalid uid
#define START_UID 10    // 1~9 reserve for funture usage

static _Atomic unsigned int cli_count = 0;
static int uid = START_UID;

typedef enum {
	COMMON,
	PRIVATE,
} chatStatus_e;

/* Client structure */
typedef struct {
	struct sockaddr_in addr;	/* Client remote address */
	int connfd;					/* Connection file descriptor */
	int uid;					/* Client unique identifier */
	int status;					/* common/private */
	int peer;
	char name[32];				/* Client name */
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static char topic[BUFFER_SZ/2];

pthread_mutex_t topic_mutex = PTHREAD_MUTEX_INITIALIZER;


/* The 'strdup' function is not available in the C standard  */
char *_strdup(const char *s) {
	size_t size = strlen(s) + 1;
	char *p = malloc(size);
	if (p) {
		memcpy(p, s, size);
	}
	return p;
}

/* Add client to queue */
void queue_add(client_t *cl){
	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (!clients[i]) {
			clients[i] = cl;
			break;
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

/* Delete client from queue */
void queue_delete(int uid){
	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (clients[i]) {
			if (clients[i]->uid == uid) {
				clients[i] = NULL;
				break;
			}
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

/* Send message to all clients but the sender */
void send_message(char *s, int uid){
	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (clients[i]) {
			if (clients[i]->uid != uid) {
				if (send(clients[i]->connfd, s, strlen(s), 0) < 0) {
					perro("Write to descriptor failed");
					break;
				}
			}
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

/* Send message to all clients */
void send_message_all(char *s){
	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i <MAX_CLIENTS; ++i){
		if (clients[i]) {
			if (send(clients[i]->connfd, s, strlen(s), 0) < 0) {
				perro("Write to descriptor failed");
				break;
			}
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

/* Send message to sender */
void send_message_self(const char *s, int connfd){
	if (send(connfd, s, strlen(s), 0) < 0) {
		perro("Write to descriptor failed");
		exit(-1);
	}
}

void  set_peer(int uid, int peer, int status) {
	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i < MAX_CLIENTS; ++i){
		if (clients[i]) {
			if (clients[i]->uid == uid) {
				clients[i]->status = status;
				clients[i]->peer = peer;
				break;
			}
		}
	}
	pthread_mutex_unlock(&clients_mutex);
	return;
}
/* Send message to client */
void send_message_client(char *s, int uid){
	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i < MAX_CLIENTS; ++i){
		if (clients[i]) {
			if (clients[i]->uid == uid) {
				if (send(clients[i]->connfd, s, strlen(s), 0)<0) {
					perro("Write to descriptor failed");
					break;
				}
			}
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

/* Send list of active clients */
void send_active_clients(int connfd){
	char s[64];

	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i < MAX_CLIENTS; ++i){
		if (clients[i]) {
			sprintf(s, "[%d] %s\r\n", clients[i]->uid, clients[i]->name);
			send_message_self(s, connfd);
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

/* Strip CRLF */
void strip_newline(char *s){
	while (*s != '\0') {
		if (*s == '\r' || *s == '\n') {
			*s = '\0';
		}
		s++;
	}
}

/* Print ip address */
void print_client_addr(struct sockaddr_in addr){
	printf("%d.%d.%d.%d",
			addr.sin_addr.s_addr & 0xff,
			(addr.sin_addr.s_addr & 0xff00) >> 8,
			(addr.sin_addr.s_addr & 0xff0000) >> 16,
			(addr.sin_addr.s_addr & 0xff000000) >> 24);
}

/* Handle all communication with the client */
void *handle_client(void *arg){
	char buff_out[BUFFER_SZ];
	char buff_in[BUFFER_SZ / 2];
	int rlen;

	cli_count++;
	client_t *cli = (client_t *)arg;

	printf("<< accept ");
	print_client_addr(cli->addr);
	printf(" referenced by %d\n", cli->uid);

	sprintf(buff_out, "%s has joined\r\n", cli->name);
	send_message_all(buff_out);

	pthread_mutex_lock(&topic_mutex);
	if (strlen(topic)) {
		buff_out[0] = '\0';
		sprintf(buff_out, "topic: %s\r\n", topic);
		send_message_self(buff_out, cli->connfd);
	}
	pthread_mutex_unlock(&topic_mutex);

	send_message_self("see /help for assistance\r\n", cli->connfd);

	/* Receive input from client */
	while ((rlen = recv(cli->connfd, buff_in, sizeof(buff_in) - 1, 0)) > 0) {
		buff_in[rlen] = '\0';
		buff_out[0] = '\0';
		strip_newline(buff_in);

		/* Ignore empty buffer */
		if (!strlen(buff_in)) {
			continue;
		}

		/* Special options */
		if (buff_in[0] == '/') {
			char *command, *param;
			command = strtok(buff_in," ");
			if (!strcmp(command, "/quit")) {
				break;
			} else if (!strcmp(command, "/topic")) {
				param = strtok(NULL, " ");
				if (param) {
					pthread_mutex_lock(&topic_mutex);
					topic[0] = '\0';
					while (param != NULL) {
						strcat(topic, param);
						strcat(topic, " ");
						param = strtok(NULL, " ");
					}
					pthread_mutex_unlock(&topic_mutex);
					sprintf(buff_out, "topic changed to: %s \r\n", topic);
					send_message_all(buff_out);
				} else {
					send_message_self("message cannot be null\r\n", cli->connfd);
				}
			} else if (!strcmp(command, "/nick")) {
				param = strtok(NULL, " ");
				if (param) {
					char *old_name = _strdup(cli->name);
					if (!old_name) {
						perro("Cannot allocate memory");
						continue;
					}
					strncpy(cli->name, param, sizeof(cli->name));
					cli->name[sizeof(cli->name)-1] = '\0';
					sprintf(buff_out, "%s is now known as %s\r\n", old_name, cli->name);
					free(old_name);
					send_message_all(buff_out);
				} else {
					send_message_self("name cannot be null\r\n", cli->connfd);
				}
			} else if (!strcmp(command, "/msg")) {
				param = strtok(NULL, " ");
				if (param) {
					int uid = atoi(param);
					cli->status = PRIVATE;
					cli->peer = uid;
					set_peer(uid, cli->uid, PRIVATE);
					send_message_self("start private talk!\n", cli->connfd);
					sprintf(buff_out, "user %s invite you to join talk!\n", cli->name);
					send_message_client(buff_out, cli->peer);
					/** param = strtok(NULL, " "); */
					/** if (param) { */
					/**	 sprintf(buff_out, "[PM][%s]", cli->name); */
					/**	 while (param != NULL) { */
					/**		 strcat(buff_out, " "); */
					/**		 strcat(buff_out, param); */
					/**		 param = strtok(NULL, " "); */
					/**	 } */
					/**	 strcat(buff_out, "\r\n"); */
					/**	 send_message_client(buff_out, uid); */
					/** } else { */
					/**	 send_message_self("message cannot be null\r\n", cli->connfd); */
					/** } */
				} else {
					send_message_self("reference cannot be null\r\n", cli->connfd);
				}
			} else if(!strcmp(command, "/list")) {
				sprintf(buff_out, "clients %d\r\n", cli_count);
				send_message_self(buff_out, cli->connfd);
				send_active_clients(cli->connfd);
			} else if (!strcmp(command, "/help")) {
				strcat(buff_out, "\n/quit	 Quit chatroom\r\n");
				strcat(buff_out, "/topic	<message> Set chat topic\r\n");
				strcat(buff_out, "/nick	 <name> Change nickname\r\n");
				strcat(buff_out, "/msg	  <peer uid> Enter private mode\r\n");
				strcat(buff_out, "/com	 Enter public mode\r\n");
				strcat(buff_out, "/list	 Show active clients\r\n");
				strcat(buff_out, "/help	 Show help\r\n");
				send_message_self(buff_out, cli->connfd);
			} else if (!strcmp(command, "/com")) {
				int peer_id = cli->peer;
				cli->status = COMMON;
				cli->peer = INV_UID;
				set_peer(peer_id, INV_UID, COMMON);
				send_message_self("end private talk!\n", cli->connfd);
				send_message_client("end private talk!\n", peer_id);
			} else {
				send_message_self("unknown command\r\n", cli->connfd);
			}
		} else {
			/* Send message */
			snprintf(buff_out, sizeof(buff_out), "[%s] %s\r\n", cli->name, buff_in);
			if (PRIVATE == cli->status) {
				send_message_client(buff_out, cli->peer);
			} else
				send_message(buff_out, cli->uid);
		}
	}

	/* Close connection */
	sprintf(buff_out, "%s has left\r\n", cli->name);
	send_message_all(buff_out);
	close(cli->connfd);

	/* Delete client from queue and yield thread */
	queue_delete(cli->uid);
	printf("quit ");
	print_client_addr(cli->addr);
	printf(" referenced by %d\n", cli->uid);
	free(cli);
	cli_count--;
	pthread_detach(pthread_self());

	return NULL;
}

int main(int argc, char *argv[]){
	int listenfd = 0, connfd = 0;
	struct sockaddr_in serv_addr;
	struct sockaddr_in cli_addr;
	pthread_t tid;

	if (argc != 2) {
		perro("args");
	}

	/* Socket settings */
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(argv[1]));

	/* Ignore pipe signals */
	/** signal(SIGPIPE, SIG_IGN); */

	/* Bind */
	if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
		perro("Socket binding failed");
		return EXIT_FAILURE;
	}

	/* Listen */
	if (listen(listenfd, 10) < 0) {
		perro("Socket listening failed");
		return EXIT_FAILURE;
	}

	printf("<[ SERVER STARTED ]>\n");

	/* Accept clients */
	while (1) {
		socklen_t clilen = sizeof(cli_addr);
		connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clilen);

		/* Check if max clients is reached */
		if ((cli_count + 1) == MAX_CLIENTS) {
			printf("<< max clients reached\n");
			printf("<< reject ");
			print_client_addr(cli_addr);
			printf("\n");
			close(connfd);
			continue;
		}

		/* Client settings */
		client_t *cli = (client_t *)malloc(sizeof(client_t));
		cli->addr = cli_addr;
		cli->connfd = connfd;
		uid = uid ==INV_UID?START_UID:uid;
		cli->uid = uid++;
		sprintf(cli->name, "%d", cli->uid);

		/* Add client to the queue and fork thread */
		queue_add(cli);
		pthread_create(&tid, NULL, &handle_client, (void*)cli);

		/* Reduce CPU usage */
		sleep(1);
	}

	return EXIT_SUCCESS;
}
