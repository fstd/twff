/*
 * bailsock.cpp
 *
 *  Created on: Oct 30, 2010
 *      Author: fisted
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

int bailsocket (int __domain, int __type, int __protocol, int timeout_sec) {
	timeval tv;tv.tv_sec=timeout_sec;tv.tv_usec=0;bool on=true;
	int sock = socket(__domain, __type, __protocol);
	if (sock < 0)
		{perror("socket");exit(EXIT_FAILURE);}
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	return sock;
}

ssize_t bailsendto(int __fd, const void *__buf, size_t __n, int __flags, const struct sockaddr* __addr, socklen_t __addr_len)
{
    ssize_t n = sendto(__fd, __buf, __n, __flags, __addr, __addr_len);
    if (n < 0 || ((size_t) n) < __n)
        {perror("sendto");exit(EXIT_FAILURE);}
    return n;
}

ssize_t bailrecvfrom(int __fd, void * __buf, size_t __n, int __flags, struct sockaddr* __addr, socklen_t * __addr_len)
{
    errno = 0;
    ssize_t n = recvfrom(__fd, __buf, __n, __flags, __addr, __addr_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        else {perror("recvfrom");exit(EXIT_FAILURE);}
    }
    return n;
}

struct sockaddr_in *bailmkaddr(const char *host, u_int16_t port)
{
    struct hostent *hp;
    struct sockaddr_in *server = new struct sockaddr_in;
    server->sin_family = AF_INET;
    errno = 0;
    hp = gethostbyname(host);
    if (hp == NULL) {
        if (errno == 0) return NULL;
        else {perror("gethostbyname");exit(EXIT_FAILURE);}
    }
    memcpy((char *) &server->sin_addr, (char *) hp->h_addr, hp->h_length);
    server->sin_port = htons(port);
    return server;
}
