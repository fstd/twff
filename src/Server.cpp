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
	_addr((u_int32_t) (saddrport >> 16)), _port((u_int16_t) (saddrport & 0xffff)),
	_name(NULL), _type(NULL), _map(NULL), _ver(NULL), _num_players(-1), _max_players(-1), _flags(-1), _progress(-1),
	_addr_str(NULL), _trimmed_name(NULL), _players()
{}

Server::Server(u_int32_t saddr, u_int16_t sport) :
	_addr(saddr), _port(sport),
	_name(NULL), _type(NULL), _map(NULL), _ver(NULL), _num_players(-1), _max_players(-1), _flags(-1), _progress(-1),
	_addr_str(NULL), _trimmed_name(NULL), _players()
{}

Server::~Server()
{
	free(_addr_str);free(_trimmed_name);free(_name);free(_type);free(_map);free(_ver);
	_players.clear();
}

void Server::setName(const char *s)
{
	free(_name);
	_name = s?strdup(s):NULL;
}

void Server::setGameType(const char *s)
{
	free(_type);
	_type = s?strdup(s):NULL;
}

void Server::setMap(const char *s)
{
	free(_map);
	_map = s?strdup(s):NULL;
}

void Server::setVersion(const char *s)
{
	free(_ver);
	_ver = s?strdup(s):NULL;
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

const char *Server::getTrimmedName() const
{
	if (!_trimmed_name) {
		const char *lp = _name;
		while (*lp && *lp <= 0x20)
			++lp;
		_trimmed_name = strdup(lp);
		if (strlen(_trimmed_name) > 0) {
			char *rp = _trimmed_name + strlen(_trimmed_name) - 1;
			while (rp > lp && *rp <= 0x20)
				*(rp--) = '\0';
		}
	}
	return _trimmed_name;
}

void Server::addPlayer(const char *pname, int pscore)
{
	_players.insert(new Player(pname,pscore));
}

Player::Player(const char *n, int s) :
	_name(strdup(n)), _score(s)
{}

Player::~Player()
{
	free(_name);
}
