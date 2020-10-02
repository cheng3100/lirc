# TODO
# - [ ] create new folder and put new build output in it. delete it when make clean
# - [ ] split the complie and link process as 2 divide part 
GCCPREFIX = 

CC	:= $(GCCPREFIX)gcc -pipe
GDB	:= $(GCCPREFIX)gdb
AS	:= $(GCCPREFIX)as
AR	:= $(GCCPREFIX)ar
LD	:= $(GCCPREFIX)ld
OBJCOPY	:= $(GCCPREFIX)objcopy
OBJDUMP	:= $(GCCPREFIX)objdump
NM	:= $(GCCPREFIX)nm

CFLAGS := -Wall
CFLAGS += -std=gnu11

LDFLAGS := -lpthread

PROGS = server client

all: ${PROGS}

server: server.c
	$(CC) $(CFLAGS) server.c -O2 $(LDFLAGS) -o server
	$(OBJDUMP) -S $@ > $@.asm

client: client.c
	$(CC) $(CFLAGS) client.c -O2 $(LDFLAGS) -o client
	$(OBJDUMP) -S $@ > $@.asm

# debug:server.c client.c irc.h
#     $(CC) $(CFLAGS) server.c -O0 $(LDFLAGS) -o server_dbg
#     $(CC) $(CFLAGS) client.c -O0 $(LDFLAGS) -o client_dbg
#     $(OBJDUMP) -S server_dbg > server_dbg.asm
#     $(OBJDUMP) -S client_dbg > client_dbg.asm

clean:
	$(RM) -rf server client *_dbg *.asm
