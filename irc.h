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

#endif
