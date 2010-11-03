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
	_addr((u_int32_t) (saddrport >> 16)), _port((u_int16_t) (saddrport & 0xffff)), _name(NULL), _type(NULL), _map(NULL), _ver(NULL),
	_num_players(-1), _max_players(-1), _flags(-1), _progress(-1), _addr_str(NULL), _players()
{
}

Server::Server(u_int32_t saddr, u_int16_t sport) :
	_addr(saddr), _port(sport), _name(NULL), _type(NULL), _map(NULL), _ver(NULL), _num_players(-1), _max_players(-1), _flags(-1),
	_progress(-1), _addr_str(NULL), _players()
{
}

Server::~Server()
{
	free(_addr_str);free(_name);free(_type);free(_map);free(_ver);
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

const char *Server::getAddrStr() const
{
	if (!_addr_str) {
		char *ipbuf = (char*) malloc(16); /*cant be static, wouldnt be reentrant*/
		sprintf(ipbuf, "%u.%u.%u.%u", (_addr >> 24), (_addr >> 16) & 0xff, (_addr >> 8) & 0xff, _addr & 0xff);
		_addr_str = ipbuf;
	}
	return _addr_str;
}

void Server::addPlayer(const char *pname, int pscore)
{
	_players.insert(new Player(pname, pscore));
}

void Server::clearPlayers()
{
	for (std::set<Player*>::const_iterator it = _players.begin(); it != _players.end(); ++it)
		delete *it;
	_players.clear();
}

Player::Player(const char *n, int s) :
	_name(strdup(n)), _score(s)
{
}

Player::~Player()
{
	free(_name);
}
