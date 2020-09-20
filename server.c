#include "irc.h"

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048

#define INV_UID 0           /* reserve for invalid uid */
#define START_UID 10        /* 1~9 reserve for funture usage */
#define MAX_UID (MAX_CLIENTS + START_UID)

#define UNAME_SZ   32

enum chat_status {
	COMMON,
	PRIVATE,
} chatStatus_e;

/* Client structure */
typedef struct {
	struct sockaddr_in addr;       /* Client remote address */
	int connfd;                    /* Connection file descriptor */
	int uid;                       /* Client unique identifier */
	int status;                    /* common/private */
	int peer;
	char name[UNAME_SZ];                 /* Client name */
} client_t;

/* 4byte in 32bit, 8byte in 64bit */
typedef uintptr_t usd_t;
typedef uintptr_t use_t;

#define USD_NUM ((1<<8))
#define USE_NUM ((1<<8))
/* get the uid set's directory index */
#define UDX(uid)  (((uid) >> 8) & 0xff)
/* get the uid set's entry index */
#define UTX(uid)  (uid & 0xff)

/* uid set entry flag */
#define USE_P  (1 << 16)     /* present */

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static char rname[BUFFER_SZ/2];

pthread_mutex_t rname_mutex = PTHREAD_MUTEX_INITIALIZER;

static _Atomic unsigned int cli_count = 0;
static int uid = START_UID;
static usd_t *g_usd;

/*
 * a two level hash set
 *
 * uid is divided 3 part:
 * usd(uid set directory), use(uid set entry), reserve(for uid)/flag(for hash set entry)
 *
 * usd: The low 8 bit of uid. It is used as a directory index.
 * use: The next 8 bit of uid. It is used as a entry index of a directory.
 * reserve/flag: Reserve for uid itself but used as flag area for hash set entry.
 * Only P(present) flag is used for now.
 *
 * set and unset's time complexity is O(1)
 * The minimal memory usage is 2K, max is 257K.
 *
 * |<-MSB
 * ++++++++++++++++++++++++++++++++++++++++
 * |   flag   ... |P|    usd    |  use    |
 * ++++++++++++++++++++++++++++++++++++++++
 *                  ^           ^         ^
 * | 16bit          |   8bit    |  8bit   |
 */
int us_init(usd_t **usd)
{
	if (!usd)
		return -1;
	*usd = malloc(USD_NUM * sizeof(usd_t));
	memset(*usd, 0, USD_NUM * sizeof(usd_t));

	return 0;
}

int us_set(usd_t *ud, uint32_t uid, int create)
{
	usd_t *d;
	use_t *e;

	if (!ud)
		return -1;

	d = (usd_t *)ud[UDX(uid)];
	if (d) {
		e = &d[UTX(uid)];
		if (*e & USE_P)
			return 1;	/* duplicate uid */

		*e = *e | USE_P;
	} else if (create) {
		e = malloc(USE_NUM * sizeof(use_t));
		if (!e)
			return -1;

		ud[UDX(uid)] = (uintptr_t)e;
		memset(e, 0, USE_NUM * sizeof(use_t));

		e[UTX(uid)] |= USE_P;
	}

	return 0;         /* no duplicate and set */
}

int us_unset(usd_t *ud, uint32_t uid)
{
	usd_t *d;
	use_t *e;

	if (!ud)
		return -1;

	d = (usd_t *)ud[UDX(uid)];
	if (!d)
		return -1;

	e = &d[UTX(uid)];
	if (*e & USE_P)
		return -1;

	*e &= ~USE_P;

	return 0;
}

int us_deinit(usd_t *usd)
{
	if (!usd)
		return -1;

	for (int i=0; i<USD_NUM; i++) {
		if (!usd[i])
			continue;
		free((use_t *)usd[i]);
	}

	free(usd);

	return 0;
}


/* Add client to queue */
void queue_add(client_t *cl)
{
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
void queue_delete(int uid)
{
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

void send_message(char *s, int uid)
{
	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (clients[i]) {
			if (clients[i]->uid != uid) {
				/** if (send(clients[i]->connfd, pack_buf, len, 0) < 0) { */
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
void send_message_all(char *s)
{
	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i <MAX_CLIENTS; ++i){
		if (clients[i]) {
			/** if (send(clients[i]->connfd, pack_buf, len, 0) < 0) { */
			if (send(clients[i]->connfd, s, strlen(s), 0) < 0) {
				perro("Write to descriptor failed");
				break;
			}
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

/* Send message to sender */
void send_message_self(char *s, int connfd)
{
	if (send(connfd, s, strlen(s), 0) < 0) {
		perro("Write to descriptor failed");
		exit(-1);
	}
}

void  set_peer(int uid, int peer, int status)
{
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
void send_message_client(char *s, int uid)
{
	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i < MAX_CLIENTS; ++i){
		if (clients[i]) {
			if (clients[i]->uid == uid) {
				/** if (send(clients[i]->connfd, pack_buf, len, 0)<0) { */
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
void send_active_clients(int connfd)
{
	char s[64];

	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i < MAX_CLIENTS; ++i){
		if (clients[i]) {
			snprintf(s, 63, "uid: [%d] name:%s\r\n", clients[i]->uid, clients[i]->name);
			send_message_self(s, connfd);
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

/* Strip CRLF */
void strip_newline(char *s)
{
	while (*s != '\0') {
		if (*s == '\r' || *s == '\n') {
			*s = '\0';
		}
		s++;
	}
}

/* Print ip address */
void print_client_addr(struct sockaddr_in addr)
{
	printf("%d.%d.%d.%d",
			addr.sin_addr.s_addr & 0xff,
			(addr.sin_addr.s_addr & 0xff00) >> 8,
			(addr.sin_addr.s_addr & 0xff0000) >> 16,
			(addr.sin_addr.s_addr & 0xff000000) >> 24);
}

int serve_cmd_process(char *buff_out, int obuf_size, char *buff_in, client_t *cli)
{
	char *command, *param;
	char *save_str;
	command = strtok_r(buff_in," ", &save_str);
	if (!strncmp(command, op_cmd[QUIT], strlen(op_cmd[QUIT]))) {
		return 1;					/* disconnect */
	} else if (!strncmp(command, op_cmd[RNAME], strlen(op_cmd[RNAME]))) {
		param = strtok_r(NULL, " ", &save_str);
		if (param) {
			pthread_mutex_lock(&rname_mutex);
			rname[0] = '\0';
			while (param != NULL) {
				strncat(rname, param, strlen(param));
				strncat(rname, " ", 1);
				param = strtok_r(NULL, " ", &save_str);
			}
			pthread_mutex_unlock(&rname_mutex);
			snprintf(buff_out, obuf_size - 1, "room name changed to: %s \r\n", rname);
			send_message_all(buff_out);
		} else {
			send_message_self("message cannot be null\r\n", cli->connfd);
		}
	} else if (!strncmp(command, op_cmd[UNAME], strlen(op_cmd[UNAME]))) {
		param = strtok_r(NULL, " ", &save_str);
		if (param) {
			char *old_name = strndup(cli->name, sizeof(cli->name));
			if (!old_name) {
				perro("Cannot allocate memory");
				return 2;
			}

			strncpy(cli->name, param, sizeof(cli->name));
			cli->name[sizeof(cli->name)-1] = '\0';
			snprintf(buff_out, obuf_size - 1, "%s is now known as %s\r\n", old_name, cli->name);
			free(old_name);
			send_message_all(buff_out);
		} else {
			send_message_self("name cannot be null\r\n", cli->connfd);
		}
	} else if (!strncmp(command, op_cmd[PRIMODE], strlen(op_cmd[PRIMODE]))) {
		param = strtok_r(NULL, " ", &save_str);
		if (param) {
			int uid = atoi(param);
			cli->status = PRIVATE;
			cli->peer = uid;
			set_peer(uid, cli->uid, PRIVATE);
			send_message_self("start private talk!\n", cli->connfd);
			snprintf(buff_out, obuf_size, "user %s invite you to join talk!\n", cli->name);
			send_message_client(buff_out, cli->peer);
		} else {
			send_message_self("reference cannot be null\r\n", cli->connfd);
		}
	} else if(!strncmp(command, op_cmd[LIST], strlen(op_cmd[LIST]))) {
		snprintf(buff_out, obuf_size, "your id: %d\nclient num: %d\r\n", cli->uid, cli_count);
		send_message_self(buff_out, cli->connfd);
		send_active_clients(cli->connfd);
	} else if (!strncmp(command, op_cmd[HELP], strlen(op_cmd[HELP]))) {
		strncat(buff_out, "\n/quit	 Quit chatroom\r\n", \
				strlen("\n/quit	 Quit chatroom\r\n"));
		strncat(buff_out, "/rname <message> Set chat room name\r\n", \
				strlen("/rname <message> Set chat room name\r\n"));
		strncat(buff_out, "/uname <name> Change username\r\n", \
				strlen("/uname <name> Change username\r\n"));
		strncat(buff_out, "/msg	  <peer uid> Enter private mode\r\n", \
				strlen("/msg	  <peer uid> Enter private mode\r\n"));
		strncat(buff_out, "/com	 Enter public mode\r\n", \
				strlen("/com	 Enter public mode\r\n"));
		strncat(buff_out, "/list	 Show active clients\r\n", \
				strlen("/list	 Show active clients\r\n"));
		strncat(buff_out, "/help	 Show help\r\n", \
				strlen("/help	 Show help\r\n"));
		send_message_self(buff_out, cli->connfd);
	} else if (!strncmp(command, op_cmd[COMMODE], strlen(op_cmd[COMMODE]))) {
		int peer_id = cli->peer;
		cli->status = COMMON;
		cli->peer = INV_UID;
		set_peer(peer_id, INV_UID, COMMON);
		send_message_self("end private talk!\n", cli->connfd);
		send_message_client("end private talk!\n", peer_id);
	} else {
		send_message_self("unknown command\r\n", cli->connfd);
	}

	return 0;
}

/* Handle all communication with the client */
void *handle_client(void *arg)
{
	char buff_out[BUFFER_SZ];
	char buff_in[BUFFER_SZ / 2];
	int rlen;

	cli_count++;
	client_t *cli = (client_t *)arg;

	printf("<< accept ");
	print_client_addr(cli->addr);
	printf(" referenced by %d\n", cli->uid);

	buff_out[BUFFER_SZ - 1] = '\0';
	snprintf(buff_out, BUFFER_SZ - 1, "%s has joined\r\n", cli->name);
	send_message_all(buff_out);

	pthread_mutex_lock(&rname_mutex);
	if (strlen(rname)) {
		buff_out[0] = '\0';
		snprintf(buff_out, BUFFER_SZ - 1, "room name: %s\r\n", rname);
		send_message_self(buff_out, cli->connfd);
	}
	pthread_mutex_unlock(&rname_mutex);

	send_message_self("see /help for assistance\r\n", cli->connfd);

	/* Receive input from client */
	while ((rlen = recv(cli->connfd, buff_in, sizeof(buff_in) - 1, 0)) > 0) {
		buff_in[rlen] = '\0';
		strip_newline(buff_in);

		tlv_t *tlv;
		tlv_parse((uint8_t*)buff_in, &tlv);
		switch (tlv->t) {
			int ret;
			case COMMAND: {
				printf("command send from %s\n", cli->name);
				ret = serve_cmd_process(buff_out, BUFFER_SZ, (char *)(tlv->v), cli);
				if (1 == ret) {  // /quit
					goto DISCONNECT;
				} else if (2 == ret) {
					continue;		// cmd fail
				}
				break;
			}
			case MESSAGE: {
				snprintf(buff_out, sizeof(buff_out), "[%s] %s\r\n", cli->name, (char *)(tlv->v));
				if (PRIVATE == cli->status) {
					send_message_client(buff_out, cli->peer);
					printf("private send from %d to %d\n", cli->uid, cli->peer);

				} else {
					send_message(buff_out, cli->uid);
					printf("public send from %s\n", cli->name);
				}
				break;
			}
			default:
			send_message_self("unkonw message\r\n", cli->connfd);
			break;
		}
		/** if (buff_in[0] == '/') { */
		/**     char *command, *param; */
		/**     command = strtok(buff_in," "); */
		/**     if (!strcmp(command, "/quit")) { */
		/**         break; */
		/**     } else if (!strcmp(command, "/topic")) { */
		/**         param = strtok(NULL, " "); */
		/**         if (param) { */
		/**             pthread_mutex_lock(&topic_mutex); */
		/**             topic[0] = '\0'; */
		/**             while (param != NULL) { */
		/**                 strcat(topic, param); */
		/**                 strcat(topic, " "); */
		/**                 param = strtok(NULL, " "); */
		/**             } */
		/**             pthread_mutex_unlock(&topic_mutex); */
		/**             sprintf(buff_out, "topic changed to: %s \r\n", topic); */
		/**             send_message_all(buff_out); */
		/**         } else { */
		/**             send_message_self("message cannot be null\r\n", cli->connfd); */
		/**         } */
		/**     } else if (!strcmp(command, "/nick")) { */
		/**         param = strtok(NULL, " "); */
		/**         if (param) { */
		/**             char *old_name = _strdup(cli->name); */
		/**             if (!old_name) { */
		/**                 perro("Cannot allocate memory"); */
		/**                 continue; */
		/**             } */
		/**             strncpy(cli->name, param, sizeof(cli->name)); */
		/**             cli->name[sizeof(cli->name)-1] = '\0'; */
		/**             sprintf(buff_out, "%s is now known as %s\r\n", old_name, cli->name); */
		/**             free(old_name); */
		/**             send_message_all(buff_out); */
		/**         } else { */
		/**             send_message_self("name cannot be null\r\n", cli->connfd); */
		/**         } */
		/**     } else if (!strcmp(command, "/msg")) { */
		/**         param = strtok(NULL, " "); */
		/**         if (param) { */
		/**             int uid = atoi(param); */
		/**             cli->status = PRIVATE; */
		/**             cli->peer = uid; */
		/**             set_peer(uid, cli->uid, PRIVATE); */
		/**             send_message_self("start private talk!\n", cli->connfd); */
		/**             sprintf(buff_out, "user %s invite you to join talk!\n", cli->name); */
		/**             send_message_client(buff_out, cli->peer); */
		/**             [> param = strtok(NULL, " "); <] */
		/**             [> if (param) { <] */
		/**             [>	 sprintf(buff_out, "[PM][%s]", cli->name); <] */
		/**             [>	 while (param != NULL) { <] */
		/**             [>		 strcat(buff_out, " "); <] */
		/**             [>		 strcat(buff_out, param); <] */
		/**             [>		 param = strtok(NULL, " "); <] */
		/**             [>	 } <] */
		/**             [>	 strcat(buff_out, "\r\n"); <] */
		/**             [>	 send_message_client(buff_out, uid); <] */
		/**             [> } else { <] */
		/**             [>	 send_message_self("message cannot be null\r\n", cli->connfd); <] */
		/**             [> } <] */
		/**         } else { */
		/**             send_message_self("reference cannot be null\r\n", cli->connfd); */
		/**         } */
		/**     } else if(!strcmp(command, "/list")) { */
		/**         sprintf(buff_out, "clients %d\r\n", cli_count); */
		/**         send_message_self(buff_out, cli->connfd); */
		/**         send_active_clients(cli->connfd); */
		/**     } else if (!strcmp(command, "/help")) { */
		/**         strcat(buff_out, "\n/quit	 Quit chatroom\r\n"); */
		/**         strcat(buff_out, "/topic	<message> Set chat topic\r\n"); */
		/**         strcat(buff_out, "/nick	 <name> Change nickname\r\n"); */
		/**         strcat(buff_out, "/msg	  <peer uid> Enter private mode\r\n"); */
		/**         strcat(buff_out, "/com	 Enter public mode\r\n"); */
		/**         strcat(buff_out, "/list	 Show active clients\r\n"); */
		/**         strcat(buff_out, "/help	 Show help\r\n"); */
		/**         send_message_self(buff_out, cli->connfd); */
		/**     } else if (!strcmp(command, "/com")) { */
		/**         int peer_id = cli->peer; */
		/**         cli->status = COMMON; */
		/**         cli->peer = INV_UID; */
		/**         set_peer(peer_id, INV_UID, COMMON); */
		/**         send_message_self("end private talk!\n", cli->connfd); */
		/**         send_message_client("end private talk!\n", peer_id); */
		/**     } else { */
		/**         send_message_self("unknown command\r\n", cli->connfd); */
		/**     } */
		/** } else { */
		/**     / Send message / */
		/**     snprintf(buff_out, sizeof(buff_out), "[%s] %s\r\n", cli->name, buff_in); */
		/**     if (PRIVATE == cli->status) { */
		/**         send_message_client(buff_out, cli->peer); */
		/**     } else */
		/**         send_message(buff_out, cli->uid); */
		/** } */
	}

DISCONNECT:
	/* Close connection */
	snprintf(buff_out, BUFFER_SZ - 1, "%s has left\r\n", cli->name);
	send_message_all(buff_out);
	close(cli->connfd);

	/* Delete client from queue and detach thread */
	queue_delete(cli->uid);
	printf("quit ");
	print_client_addr(cli->addr);
	printf(" referenced by %d\n", cli->uid);
	us_unset(g_usd, cli->uid);
	free(cli);
	cli_count--;
	pthread_detach(pthread_self());

	return NULL;
}

int main(int argc, char *argv[])
{
	int listenfd = 0, connfd = 0;
	struct sockaddr_in serv_addr;
	struct sockaddr_in cli_addr;
	pthread_t tid;

	if (argc != 2)
		perro("args");

	/* Socket settings */
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(argv[1]));

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

	us_init(&g_usd);

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

		/* find a nonredundant uid from the uid set */
		do {
			uid++;
			uid = (uid > MAX_UID)?START_UID:uid;
		} while (us_set(g_usd, uid, 1));

		cli->uid = uid;
		snprintf(cli->name, UNAME_SZ, "%d", cli->uid);

		/* Add client to the queue and fork thread */
		queue_add(cli);
		pthread_create(&tid, NULL, &handle_client, (void*)cli);

		/* Reduce CPU usage */
		sleep(1);
	}

	/* Connection over */
	close(listenfd);
	us_deinit(g_usd);

	return EXIT_SUCCESS;
}
