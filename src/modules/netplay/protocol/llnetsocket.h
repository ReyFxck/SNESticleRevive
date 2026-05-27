
#ifndef _LLNETSOCKET_H
#define _LLNETSOCKET_H

#include <tamtypes.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _EE
#include <ps2ip.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#else
#include <sysmem.h>
#include <kernel.h>
#include <sifcmd.h>
#include <sifrpc.h>
#include <ps2ip.h>
#define closesocket disconnect
#endif

#ifndef SOCKET
#define SOCKET int
#endif

typedef struct
{
	unsigned char  sin_len;
	unsigned char  sin_family;
	unsigned short sin_port;
	unsigned int   sin_addr;
	char           sin_zero[8];
} NetSocketAddrT;

#endif

