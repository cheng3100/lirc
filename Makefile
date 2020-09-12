PROGS = server client

all: ${PROGS}

server: server.c
	$(CC) -Wall server.c -O2 -std=c11 -lpthread -o server

client: client.c
	$(CC)  client.c -O2 -std=c11 -lpthread -o client

# debug:
#     $(CC) -Wall -g chat_server.c -O0 -std=c11 -lpthread -o chat_server_dbg

clean:
	$(RM) -rf server client 
