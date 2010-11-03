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

		mutable char *_addr_str;

		std::set<class Player*> _players;

	public:
		Server(u_int64_t saddrport);
		Server(u_int32_t saddr, u_int16_t sport);
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
		const char *getGameType() const {return _type;}
		const char *getMap() const      {return _map;}
		const char *getVersion() const  {return _ver;}
		int getNumPlayers() const       {return _num_players;}
		int getMaxPlayers() const       {return _max_players;}
		int getFlags() const            {return _flags;}
		int getProgress() const         {return _progress;}

		const char *getAddrStr() const;

		const std::set<class Player*>& pmap() const {return _players;}
		void addPlayer(const char *pname, int pscore);
		void clearPlayers();
};

class Player
{
	private:
		char *_name;
		int _score;

	public:
		Player(const char *n, int s);
		virtual ~Player();

		const char *getName() const {return _name;}
		int getScore() const        {return _score;}
};

#endif /* SERVER_H_ */
