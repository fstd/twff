/*
 * bailsock.cpp
 *
 *  Created on: Oct 30, 2010
 *      Author: fisted
 */

#include <netinet/in.h>
#include <netdb.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include "bailsock.h"

#define DBG(LVL,ARG...) do { if (g_str_err && g_verb >= (LVL)) fprintf(g_str_err,ARG); } while(0)

extern int g_verb;
extern FILE *g_str_err;

int bailsocket (int dom, int type, int prot, int timeout_sec)
{
	bool on=true;
	int sock;
	timeval tv;

	tv.tv_sec=timeout_sec;tv.tv_usec=0;
	sock = socket(dom, type, prot);
	if (sock < 0)
		{perror("socket");exit(EXIT_FAILURE);}
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	return sock;
}

ssize_t bailsendto(int fd, const void *buf, size_t n, int flg, const struct sockaddr* saddr, socklen_t addrlen)
{
	ssize_t r;

	r = sendto(fd, buf, n, flg, saddr, addrlen);
	if (r < 0 || ((size_t) r) < n)
		{perror("sendto");exit(EXIT_FAILURE);}
	return r;
}

ssize_t bailrecvfrom(int fd, void * buf, size_t n, int flg, struct sockaddr* saddr, socklen_t * addrlen)
{
	ssize_t r;

	errno = 0;
	r = recvfrom(fd, buf, n, flg, saddr, addrlen);
	if (r < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
		else {perror("recvfrom");exit(EXIT_FAILURE);}
	}
	return r;
}

struct sockaddr_in *bailmkaddr(const char *host, u_int16_t port)
{
	struct hostent *res = NULL, *hp;
	struct sockaddr_in *server;
	char ghbnbuf[64], *overbuf=NULL, *curbuf = ghbnbuf;
	size_t bufsz = sizeof(ghbnbuf);
	int errvar,retval;

	hp  = new struct hostent;
	server = new struct sockaddr_in;

	server->sin_family = AF_INET;
	while((retval = gethostbyname_r(host,hp,curbuf,bufsz,&res,&errvar)) == ERANGE) {
		free(overbuf);
		DBG(2, "resolvbuf too small (was: %i) now trying with size %i\n",bufsz,bufsz<<1);
		curbuf = overbuf = (char*)malloc(bufsz<<=1);
	}

//	if (retval != 0)
//		{perror("gethostbyname_r");exit(EXIT_FAILURE);}

	if (retval != 0 || !res) {
		DBG(2, "failed to mkaddr for %s:%i, errorcode: %i\n",host,port, errvar);
		free(overbuf);
		delete hp;
		delete server;
		return NULL;
	}

	memcpy((char *) &server->sin_addr, (char *) res->h_addr, res->h_length);
	server->sin_port = htons(port);
	free(overbuf);
	delete hp;
	return server;
}
