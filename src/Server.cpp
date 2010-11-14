/*
 * Server.cpp
 *
 *  Created on: Oct 30, 2010
 *      Author: fisted
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Server.h"

Server::Server(u_int64_t saddrport) :
	_addr((u_int32_t) (saddrport >> 16)), _port((u_int16_t) (saddrport & 0xffffu)), _name(NULL), _type(NULL),
	_map(NULL), _ver(NULL), _num_players(-1), _max_players(-1), _flags(-1), _progress(-1), _host_str(NULL),
	_ip_str(NULL), _players()
{
}

Server::Server(u_int32_t saddr, u_int16_t sport) :
	_addr(saddr), _port(sport), _name(NULL), _type(NULL), _map(NULL), _ver(NULL), _num_players(-1),
	_max_players(-1), _flags(-1), _progress(-1), _host_str(NULL), _ip_str(NULL), _players()
{
}

Server::Server(const char *addrport) :
	_name(NULL), _type(NULL), _map(NULL), _ver(NULL),
	_num_players(-1), _max_players(-1), _flags(-1), _progress(-1), _host_str(NULL), _ip_str(NULL), _players()
{
	unsigned int v[5];
	sscanf(addrport, "%u.%u.%u.%u:%u", v, v + 1, v + 2, v + 3, v + 4);
	_addr = ((v[0] & 0xffu) << 24) | ((v[1] & 0xffu) << 16)
			| ((v[2] & 0xffu) << 8) | (v[3] & 0xffu);
	_port = (v[4] & 0xffffu);
}

Server::~Server()
{
	free(_host_str);free(_ip_str);free(_name);free(_type);free(_map);free(_ver);
	clearPlayers();
}

void Server::setName(const char *s)
{
	free(_name);
	_name = s ? strdup(s) : NULL;
}

void Server::setGameType(const char *s)
{
	free(_type);
	_type = s ? strdup(s) : NULL;
}

void Server::setMap(const char *s)
{
	free(_map);
	_map = s ? strdup(s) : NULL;
}

void Server::setVersion(const char *s)
{
	free(_ver);
	_ver = s ? strdup(s) : NULL;
}

const char *Server::getHost() const
{
	char *ipbuf; /*cant be static, wouldnt be reentrant*/

	if (!_host_str) {
		ipbuf = (char*) malloc(22);
		sprintf(ipbuf, "%u.%u.%u.%u:%i", (_addr >> 24), (_addr >> 16) & 0xff,
				(_addr >> 8) & 0xff, _addr & 0xff, _port);
		_host_str = ipbuf;
	}
	return _host_str;
}

const char *Server::getIP() const
{
	char *ipbuf;  /*cant be static, wouldnt be reentrant*/

	if (!_ip_str) {
		ipbuf = (char*) malloc(16);
		sprintf(ipbuf, "%u.%u.%u.%u", (_addr >> 24), (_addr >> 16) & 0xff,
				(_addr >> 8) & 0xff, _addr & 0xff);
		_ip_str = ipbuf;
	}
	return _ip_str;
}

void Server::addPlayer(const char *pname, int pscore)
{
	_players.insert(new Player(this, pname, pscore));
}

void Server::clearPlayers()
{
	std::set<Player*>::const_iterator it_pl;

	for (it_pl = _players.begin(); it_pl != _players.end(); ++it_pl)
		delete *it_pl;
	_players.clear();
}

Player::Player(class Server *srv, const char *n, int s) :
	_name(strdup(n)), _srv(srv), _score(s)
{
}

Player::~Player()
{
	free(_name);
}
