/*
 * bailsock.c
 *
 *  Created on: Oct 30, 2010
 *      Author: fisted
 */

#ifndef BAILSOCK_H_
#define BAILSOCK_H_

#include <sys/types.h>
#include <sys/socket.h>

/*for simplicity, these just wrap socket functions and perror()+exit() on any errors.*/

int bailsocket (int __domain, int __type, int __protocol, int timeout_sec);
ssize_t bailsendto (int __fd, const void *__buf, size_t __n,int __flags, const struct sockaddr* __addr,socklen_t __addr_len);
ssize_t bailrecvfrom (int __fd, void * __buf, size_t __n,int __flags, struct sockaddr* __addr,socklen_t * __addr_len);
struct sockaddr_in *bailmkaddr(const char *host, u_int16_t port);

#endif
