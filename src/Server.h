/*
 * Server.h
 *
 *  Created on: Oct 30, 2010
 *      Author: fisted
 */

#ifndef SERVER_H_
#define SERVER_H_

#include <set>

#include <sys/types.h>

class Server
{
	private:
		u_int32_t _addr;
		u_int16_t _port;

		char *_name, *_type, *_map, *_ver;
		int _num_players, _max_players, _flags, _progress;

		mutable char *_host_str;
		mutable char *_ip_str;

		std::set<class Player*> _players;

	public:
		Server(u_int64_t saddrport);
		Server(u_int32_t saddr, u_int16_t sport);
		Server(const char *addrport);
		virtual ~Server();

		u_int16_t getPort() const       {return _port;}
		u_int32_t getAddr() const       {return _addr;}

		void setName(const char *s);
		void setGameType(const char *s);
		void setMap(const char *s);
		void setVersion(const char *s);
		void setNumPlayers(int i)       {_num_players = i;}
		void setMaxPlayers(int i)       {_max_players = i;}
		void setFlags(int i)            {_flags = i;}
		void setProgress(int i)         {_progress = i;}

		const char *getName() const     {return _name;}
		const char *getType() const     {return _type;}
		const char *getMap() const      {return _map;}
		const char *getVer() const      {return _ver;}
		int getNumPl() const            {return _num_players;}
		int getMaxPl() const            {return _max_players;}
		int getFlags() const            {return _flags;}
		int getProgress() const         {return _progress;}

		const char *getHost() const;
		const char *getIP() const;

		const std::set<class Player*>& pmap() const {return _players;}
		void addPlayer(const char *pname, int pscore);
		void clearPlayers();
};

class Player
{
	private:
		char *_name;
		class Server *_srv;
		int _score;

	public:
		Player(class Server *srv, const char *name, int score);
		virtual ~Player();

		const char *getName() const     {return _name;}
		class Server *getServer() const {return _srv;}
		int getScore() const            {return _score;}
};

#endif /* SERVER_H_ */
