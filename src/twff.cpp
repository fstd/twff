/*
 * twff.cpp
 *
 *  Created on: Oct 30, 2010
 *      Author: fisted
 */

#include <set>
#include <list>
#include <string>

#include <netinet/in.h>
#include <pthread.h>
#include <regex.h>
#include <sys/time.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cerrno>

#include "bailsock.h"
#include "Server.h"


#define DEFAULT_MASTERPORT 8300
#define DATA_OFFSET 14


std::list<class Server*> list_work;
std::list<class Server*> list_done;
std::list<class Server*> list_fail;

std::list<const char*> list_masters;

pthread_mutex_t mutex_work;
FILE *str_out                    = stdout;
FILE *str_raw                    = NULL;

int recv_timeout_ms              = 3;
int recv_timeout_gs              = 3;
int num_threads                  = 20;
int verbosity                    = 0;
int num_retry_gs                 = 2;
int num_retry_ms                 = 2;
int lead_nl                      = 0;
int players_per_line             = 1;
bool no_status_msg               = false;
bool summary                     = false;
bool output_append               = false;
bool raw_output_append           = false;
bool force_master_complete       = false;
bool colored                     = false;
bool case_insensitive            = false;
bool hide_empty_srv              = false;
bool srv_show_players            = false;
const char *output_file          = "-";
const char *raw_output_file      = NULL;

const char *player_expr          = ".*";
const char *srvtype_expr         = NULL;
const char *srvname_expr         = NULL;
const char *srvaddr_expr         = NULL;
const char *srvmap_expr          = NULL;
const char *srvver_expr          = NULL;

const unsigned char P_GETCOUNT[] = {0xff,0xff,0xff,0xff,0x63,0x6f,0x75,0x6e};
const unsigned char P_GETLIST[]  = {0xff,0xff,0xff,0xff,0x72,0x65,0x71,0x74};
const unsigned char P_GETINFO[]  = {0xff,0xff,0xff,0xff,0x67,0x69,0x65,0x66};
const int P_INFO_LEN             = 8;
const int P_LIST_LEN             = 8;


void request_serverlists(std::list<u_int64_t> *dest, int *numSucMasters);
int request_serverlist(std::list<u_int64_t> *dest, const char *masterHost, u_int16_t masterPort, int numRetry, bool tryComplete);
int perform_req_srvlist(std::list<u_int64_t> *dest, const char *masterHost, u_int16_t masterPort, int *numSrvAnnounced);

bool request_details(class Server *srv, int numRetry);
bool perform_req_details(class Server *srv);

int construct_packet(unsigned char *buffer, const unsigned char *pkt, size_t len);
int construct_getlist(unsigned char *buffer);
int construct_getinfo(unsigned char *buffer);
int construct_getcount(unsigned char *buffer);

void *process_queue(void*arg);

void output_servers();
void output_players();

bool check_plinfo_sanity(const char *name, const char *score);
bool check_srvinfo_sanity(const char *ver, const char *name, const char *map, const char *type, const char *flags, const char *prog, const char *numpl, const char *maxpl);
void sanitize(char *s);
bool is_numeric(const char *s);

u_int16_t getWord(const void* buf);
u_int16_t getWordNC(const void* buf);
u_int32_t getDWord(const void* buf);
u_int32_t getDWordNC(const void* buf);

bool init(int argc, char **argv);
bool process_args(int argc, char **argv);
void usage(const char *a0, int ec) __attribute__ ((noreturn));

int main(int argc, char **argv);


void request_serverlists(std::list<u_int64_t> *dest, int *numSucMasters)
{
	char *host, *tmp;
	int port, sucm = 0;

	for (std::list<const char *>::const_iterator it = list_masters.begin(); it != list_masters.end(); ++it) {
		host = tmp = strdup(*it);
		if (strchr(tmp, ':')) {
			host = strdup(strtok(tmp, ":"));
			port = strtol(tmp + strlen(host) + 1, NULL, 10);
			free(tmp);
		} else {
			port = DEFAULT_MASTERPORT;
		}

		if (request_serverlist(dest, host, (u_int16_t) port, num_retry_ms, force_master_complete) > 0)
			++sucm;

		free(host);
	}
	if (numSucMasters)
		*numSucMasters = sucm;
}

int request_serverlist(std::list<u_int64_t> *dest, const char *masterHost, u_int16_t masterPort, int numRetry, bool tryComplete)
{
	int numsrv, numsrv_announced;
	std::list<u_int64_t> tmplist;

	if (!dest || !masterHost || !masterPort)
		return -1;

	for (int attempt = 0; numRetry < 0 || attempt <= numRetry; ++attempt) {
		tmplist.clear();
		numsrv = perform_req_srvlist(&tmplist, masterHost, masterPort, &numsrv_announced);
		if (numsrv > 0 && (!tryComplete || (numsrv >= numsrv_announced)))
			break;
	}

	for (std::list<u_int64_t>::const_iterator it = tmplist.begin(); it != tmplist.end(); ++it)
		dest->push_back(*it);

	if (verbosity >= 1) {
		if (numsrv < 0) fprintf(stderr, "failed to retrieve any server from %s:%i:\n", masterHost, masterPort);
		else            fprintf(stderr, "retrieved %i out of %i announced servers from %s:%i:\n", numsrv, numsrv_announced, masterHost, masterPort);
	}
	return numsrv;
}

int perform_req_srvlist(std::list<u_int64_t> *dest, const char *masterHost, u_int16_t masterPort, int *numSrvAnnounced)
{
	static unsigned char iobuf[1024];
	struct sockaddr_in *server = NULL;
	int sock, dlen;
	int srv_count = 0, srv_got = 0, ret = -1;

	sock = bailsocket(AF_INET, SOCK_DGRAM, 0, recv_timeout_ms);
	if (!(server = bailmkaddr(masterHost, masterPort)))
		goto prs_bailout;

	bailsendto(sock, iobuf, construct_getcount(iobuf), 0, (const sockaddr*) server, sizeof(struct sockaddr_in));

	if (bailrecvfrom(sock, iobuf, sizeof(iobuf), 0, NULL, NULL) < DATA_OFFSET + 2)
		goto prs_bailout;

	srv_count = getWord(iobuf + DATA_OFFSET);

	bailsendto(sock, iobuf, construct_getlist(iobuf), 0, (const sockaddr*) server, sizeof(struct sockaddr_in));

	while (srv_got < srv_count) {
		dlen = bailrecvfrom(sock, iobuf, sizeof(iobuf), 0, NULL, NULL);
		if (dlen < DATA_OFFSET)
			break; // recv timeout or too less data

		for (int z = DATA_OFFSET; z < dlen; z += 6)
			dest->push_back((((u_int64_t) getDWord(iobuf + z)) << 16) | getWordNC(iobuf + z + 4));

		srv_got += (dlen - DATA_OFFSET) / 6;

		if (verbosity >= 1) fprintf(stderr, "\r%i/%i          ", srv_got, srv_count);
	}
	if (numSrvAnnounced)
		*numSrvAnnounced = srv_count;

	if (verbosity >= 1 && srv_got >= 0) fprintf(stderr, "\n");

	ret = srv_got;

prs_bailout:
	if (sock >= 0) close(sock);
	if (server) delete server;
	return ret;
}

bool request_details(class Server *srv, int numRetry)
{
	if (!srv)
		return false;

	for (int attempt = 0; numRetry < 0 || attempt <= numRetry; ++attempt)
		if (perform_req_details(srv))
			return true;

	if (verbosity >= 1) fprintf(stderr, "failed to inquire %s:%i\n", srv->getAddrStr(), srv->getPort());
	return false;
}

bool perform_req_details(class Server *srv)
{
    unsigned char iobuf[1024];
    struct sockaddr_in *server = NULL;
    int sock,bcnt, len,bdone = 0;
    char *ptr;
    char *ver=NULL, *name=NULL, *map=NULL,  *type=NULL, *flags=NULL,
    	 *prog=NULL,*numpl=NULL,*maxpl=NULL,*pname=NULL,*pscore=NULL;
    bool ret = false;

    sock = bailsocket(AF_INET, SOCK_DGRAM, 0, recv_timeout_gs);
    if (!((server = bailmkaddr(srv->getAddrStr(), srv->getPort()))))
        goto prd_bailout;

    bailsendto(sock, iobuf, construct_getinfo(iobuf), 0, (const sockaddr*) server, sizeof(struct sockaddr_in));

    bcnt = bailrecvfrom(sock, iobuf, sizeof(iobuf) - 1, 0, NULL, NULL);
    if (bcnt <= DATA_OFFSET)
    	goto prd_bailout;
    iobuf[bcnt] = '\0';

    ptr = ((char*) iobuf) + DATA_OFFSET;
    bcnt -= DATA_OFFSET;

    ver      = strdup(ptr);            if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) goto prd_bailout;
	name     = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) goto prd_bailout;
	map      = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) goto prd_bailout;
	type     = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) goto prd_bailout;
	flags    = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) goto prd_bailout;
	prog     = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) goto prd_bailout;
	numpl    = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) goto prd_bailout;
	maxpl    = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >  bcnt) goto prd_bailout;

	sanitize(ver);
	sanitize(name);
	sanitize(map);
	sanitize(type);

	if (!check_srvinfo_sanity(ver,name,map,type,flags,prog,numpl,maxpl)) {
		fprintf(stderr,"crap server \"%s:%i\" - \"%s\" - \"%s\" - \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\"\n",srv->getAddrStr(),srv->getPort(),ver,name,map,type,flags,prog,numpl,maxpl);
		goto prd_bailout;
	}

	while (bdone < bcnt) {
        pname = strdup(ptr += len + 1);
        if ((bdone += (len = strlen(ptr)) + 1) >= bcnt)
        	break;/*skip last string which some ddrace servers seem to append*/;

        pscore = strdup(ptr += len + 1);
        if ((bdone += (len = strlen(ptr)) + 1) > bcnt)
        	goto prd_bailout;

        sanitize(pname);
        if (!check_plinfo_sanity(pname,pscore)) {
    		fprintf(stderr,"crap player \"%s\" \"%s\" on srv \"%s:%i\"\n",pname,pscore,srv->getAddrStr(),srv->getPort());
    		goto prd_bailout;
        }

        srv->addPlayer(pname, (int) strtol(pscore, NULL, 10));
        free(pname); free(pscore);pname=pscore=NULL;
    }

    srv->setVersion(ver);
    srv->setName(name);
    srv->setMap(map);
    srv->setGameType(type);
    srv->setFlags((int) strtol(flags, NULL, 10));
    srv->setProgress((int) strtol(prog, NULL, 10));
    srv->setNumPlayers((int) strtol(numpl, NULL, 10));
    srv->setMaxPlayers((int) strtol(maxpl, NULL, 10));

    ret=true;

prd_bailout:
    if (sock >= 0) close(sock);
	if (server) delete server;
	if (ver) free(ver);
	if (name) free(name);
	if (map) free(map);
	if (type) free(type);
	if (flags) free(flags);
	if (prog) free(prog);
	if (numpl) free(numpl);
	if (maxpl) free(maxpl);
	if (pname) free(pname);
	if (pscore) free(pscore);
	if (!ret) srv->clearPlayers();
	return ret;
}

int construct_packet(unsigned char *buffer, const unsigned char *pkt, size_t len)
{
	for (int z = 0; z < 6; ++z) buffer[z] = 0xff;
	memcpy(buffer + 6, pkt, len);
	return 6 + len;
}

int construct_getlist(unsigned char *buffer)
{
	return construct_packet(buffer, P_GETLIST, sizeof(P_GETLIST));
}

int construct_getinfo(unsigned char *buffer)
{
	return construct_packet(buffer, P_GETINFO, sizeof(P_GETINFO));
}

int construct_getcount(unsigned char *buffer)
{
	return construct_packet(buffer, P_GETCOUNT, sizeof(P_GETCOUNT));
}

void *process_queue(void*arg)
{
	int tid = *((int*) arg);
	for (;;) {
		class Server *srv = NULL;

		pthread_mutex_lock(&mutex_work);
		if (!list_work.empty()) {
			srv = list_work.front();
			list_work.pop_front();
			if (verbosity >= 1)
				fprintf(stderr, "\r%i servers remaining         ", list_work.size());
		} else {
			if (verbosity >= 1)
				fprintf(stderr, "\rthread %i done               ", tid);
			pthread_mutex_unlock(&mutex_work);
			break;
		}
		pthread_mutex_unlock(&mutex_work);

		bool ok = request_details(srv, num_retry_gs);

		pthread_mutex_lock(&mutex_work);
		(ok ? list_done : list_fail).push_back(srv);
		pthread_mutex_unlock(&mutex_work);
	}
	return NULL;
}

void output_servers()
{
	if (!srvtype_expr && !srvmap_expr && !srvname_expr && !srvver_expr && !srvaddr_expr && !str_raw) return;

	static char addrbuf[22];
	bool show, shown_one = false;

	regex_t *type_regex = srvtype_expr ? new regex_t : NULL;
	regex_t *name_regex = srvname_expr ? new regex_t : NULL;
	regex_t *addr_regex = srvaddr_expr ? new regex_t : NULL;
	regex_t *map_regex  = srvmap_expr  ? new regex_t : NULL;
	regex_t *ver_regex  = srvver_expr  ? new regex_t : NULL;

	if (type_regex && regcomp(type_regex, srvtype_expr, REG_EXTENDED | (case_insensitive ? REG_ICASE : 0)) != 0)
		{delete type_regex;type_regex = NULL;fprintf(stderr, "could not compile gametype regexp, ignoring\n");}
	if (name_regex && regcomp(name_regex, srvname_expr, REG_EXTENDED | (case_insensitive ? REG_ICASE : 0)) != 0)
		{delete name_regex;name_regex = NULL;fprintf(stderr, "could not compile srvname regexp, ignoring\n");}
	if (addr_regex && regcomp(addr_regex, srvaddr_expr, REG_EXTENDED | (case_insensitive ? REG_ICASE : 0)) != 0)
		{delete addr_regex;addr_regex = NULL;fprintf(stderr, "could not compile addr regexp, ignoring\n");}
	if (map_regex && regcomp(map_regex, srvmap_expr, REG_EXTENDED | (case_insensitive ? REG_ICASE : 0)) != 0)
		{delete map_regex;map_regex = NULL;fprintf(stderr, "could not compile map regexp, ignoring\n");}
	if (ver_regex && regcomp(ver_regex, srvver_expr, REG_EXTENDED | (case_insensitive ? REG_ICASE : 0)) != 0)
		{delete ver_regex;ver_regex = NULL;fprintf(stderr, "could not version regexp, ignoring\n");}

	for (std::list<Server*>::const_iterator it = list_done.begin(); it != list_done.end(); ++it) {
		show = false;
		Server *srv = *it;
		if (str_raw) {
			fprintf(str_raw, "S;%s:%i;%i;%i;%x;%i\n%s\n%s\n%s\n%s\n", srv->getAddrStr(), srv->getPort(), srv->getNumPlayers(),srv->getMaxPlayers(),
					srv->getFlags(), srv->getProgress(), srv->getGameType(), srv->getMap(), srv->getVersion(), srv->getName());
			if (srv->pmap().size() > 0)
				for (std::set<Player*>::const_iterator itt = srv->pmap().begin(); itt != srv->pmap().end(); ++itt)
					fprintf(str_raw, "P;%s\n", (*itt)->getName());
		}
		if (str_out) {
			sprintf(addrbuf, "%s:%i", srv->getAddrStr(), srv->getPort());

			if (         type_regex && regexec(type_regex, srv->getGameType(), 0, NULL, 0) == 0) show = true;
			if (!show && name_regex && regexec(name_regex, srv->getName(),     0, NULL, 0) == 0) show = true;
			if (!show && map_regex  && regexec(map_regex,  srv->getMap(),      0, NULL, 0) == 0) show = true;
			if (!show && ver_regex  && regexec(ver_regex,  srv->getVersion(),  0, NULL, 0) == 0) show = true;
			if (!show && addr_regex && regexec(addr_regex, addrbuf,            0, NULL, 0) == 0) show = true;

			if (show && (!hide_empty_srv || srv->pmap().size() > 0)) {
				if (!shown_one) while (lead_nl-- > 0) fprintf(str_out, "\n");
				shown_one = true;

				fprintf(str_out,"\"%s%s%s\" - %s%s - %s%s (%s%i/%i%s) - %s%s:%i%s - %s%s%s - [%s%x;%i%s]\n",
					colored?"\033[01;32m":"", srv->getName(),                      colored?"\033[0m":"",
					colored?"\033[01;31m":"", srv->getGameType(), srv->getMap(),          colored?"\033[0m":"",
					colored?"\033[01;36m":"", srv->getNumPlayers(), srv->getMaxPlayers(), colored?"\033[0m":"",
					colored?"\033[01;34m":"", srv->getAddrStr(), srv->getPort(),          colored?"\033[0m":"",
					colored?"\033[01;35m":"", srv->getVersion(),                          colored?"\033[0m":"",
					colored?"\033[01;37m":"", srv->getFlags(), srv->getProgress(),        colored?"\033[0m":"");

				if (srv_show_players) {
					int lc = 0;
					for (std::set<Player*>::const_iterator itt = srv->pmap().begin(); itt != srv->pmap().end(); ++itt) {
						fprintf(str_out, "%s\"%s%s%s\"", lc == 0 ? "\t" : "; ",
								(colored ? "\033[01;33m" : ""), (*itt)->getName(), colored ? "\033[0m" : "");
						if ((++lc) >= players_per_line) {
							fprintf(str_out, "\n");
							lc = 0;
						}
					}
					if (lc) fprintf(str_out, "\n");
				}
			}
		}
	}
	if (!no_status_msg) {
		while (lead_nl-- > 0) fprintf(str_out, "\n");
		if (shown_one) fprintf(str_out,"--- end of server listing ---\n");
		else           fprintf(str_out,"--- no server matched ---\n");
	}

	if (type_regex) {regfree(type_regex);delete type_regex;}
	if (name_regex) {regfree(name_regex);delete name_regex;}
	if (addr_regex) {regfree(addr_regex);delete addr_regex;}
	if (map_regex)  {regfree(map_regex); delete map_regex;}
	if (ver_regex)  {regfree(ver_regex); delete ver_regex;}
}

void output_players()
{
	if (!player_expr || strlen(player_expr) == 0 || !str_out) return;

	bool shown_one = false;
	regex_t *player_regex = player_expr ? new regex_t : NULL;

	if (player_regex && regcomp(player_regex, player_expr, REG_EXTENDED | (case_insensitive ? REG_ICASE : 0)) == 0) {
		for (std::list<Server*>::const_iterator it_srv = list_done.begin(); it_srv != list_done.end(); ++it_srv) {
			Server *srv = *it_srv;
			for (std::set<Player*>::const_iterator it_pl = srv->pmap().begin(); it_pl != srv->pmap().end(); ++it_pl) {
				if (regexec(player_regex, (*it_pl)->getName(), 0, NULL, 0) == 0) {
					if (!shown_one) while (lead_nl-- > 0) fprintf(str_out, "\n");
					shown_one = true;

					fprintf(str_out,"\"%s%s%s\" is on %s%s:%i%s - %s%s - %s%s (%s%i/%i%s) - \"%s%s%s\"\n",
						colored?"\033[01;33m":"", (*it_pl)->getName(),                        colored?"\033[0m":"",
						colored?"\033[01;34m":"", srv->getAddrStr(), srv->getPort(),          colored?"\033[0m":"",
						colored?"\033[01;31m":"", srv->getGameType(), srv->getMap(),          colored?"\033[0m":"",
						colored?"\033[01;36m":"", srv->getNumPlayers(), srv->getMaxPlayers(), colored?"\033[0m":"",
						colored?"\033[01;32m":"", srv->getName(),                      colored?"\033[0m":"");
				}
			}
		}
		if (!no_status_msg) {
			while (lead_nl-- > 0) fprintf(str_out, "\n");
			if (shown_one) fprintf(str_out, "--- end of players listing ---\n");
			else           fprintf(str_out, "--- no players matched ---\n");
		}
	} else {
		if (player_expr) fprintf(stderr,"cannot compile player regex\n");
	}
	if (player_regex) {regfree(player_regex); delete player_regex;}
}

bool check_plinfo_sanity(const char *name,const char *score)
{
	return (name && score && *name && *score && is_numeric(score));
}

bool check_srvinfo_sanity(const char *ver,const char *name,const char *map,const char *type,
		const char *flags,const char *prog,const char *numpl,const char *maxpl)
{
	return ver && name && map && type && prog && flags && numpl && maxpl
		&& *ver && *name && *map && *type && *prog && *flags && *numpl && *maxpl
		&& is_numeric(prog) && is_numeric(flags) && is_numeric(numpl) && is_numeric(maxpl);
}

/*replace control chars with space, then trim left and right*/
void sanitize(char *s)
{
	if (!s || strlen(s) == 0) return;

	for (unsigned char c; (c = (unsigned char)*s); ++s)
		if (c < 0x20 || c == 0xff)
			*s = 0x20;

	char *sptr = s + strlen(s);
	while (sptr > s && *sptr == 0x20) *(sptr--) = 0;

	if (sptr == s) {
		*s = 0;
	} else {
		sptr = s;
		while (*sptr == 0x20) ++sptr;
		char *res = strdup(sptr);
		strcpy(s, res);
		free(res);
	}
}

bool is_numeric(const char *s)
{
	bool gotdig = false;
	if (!s || !(*s)) return false;

	if (*s == '-' || *s == '+') ++s;

	for (char c; (c = *s); ++s)
		if (!isdigit(c)) return false;
		else             gotdig = true;

	return gotdig;
}

u_int16_t getWord(const void* buf)
{
	return ntohs(*((u_int16_t*) buf));
}

u_int16_t getWordNC(const void* buf)
{
	return *((u_int16_t*) buf);
}

u_int32_t getDWord(const void* buf)
{
	return ntohl(*((u_int32_t*) buf));
}

u_int32_t getDWordNC(const void* buf)
{
	return *((u_int32_t*) buf);
}

bool init(int argc, char **argv)
{
	if (!process_args(argc, argv)) return false;
	if (strcmp("-", output_file) != 0)
		str_out = NULL;//lazy fopen
	pthread_mutex_init(&mutex_work, NULL);
	if (list_masters.empty()) {
		list_masters.push_back("master1.teeworlds.com");
		list_masters.push_back("master2.teeworlds.com");
		list_masters.push_back("master3.teeworlds.com");
		list_masters.push_back("master4.teeworlds.com");
	}
	return true;
}

/*TODO switch to getopt & friends*/
bool process_args(int argc, char **argv)
{
	for (int z = 1; z < argc; ++z) {
		if (strcmp("-v", argv[z]) == 0) {
			++verbosity;
		} else if (strcmp("-h", argv[z]) == 0) {
			usage(argv[0], EXIT_SUCCESS);
		} else if (strcmp("-m", argv[z]) == 0) {
			if (z + 1 < argc) {
				char *mlist = strdup(argv[++z]);
				char *ptr = strtok(mlist, ",");
				if (!ptr) return false;
				do {list_masters.push_back(strdup(ptr));} while ((ptr = strtok(NULL, ",")));
				free(mlist);
			} else return false;
		} else if (strcmp("-o", argv[z]) == 0) {
			if (z + 1 < argc) output_file = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-f", argv[z]) == 0) {
			force_master_complete = true;
		} else if (strcmp("-l", argv[z]) == 0) {
			if (z + 1 < argc) players_per_line = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-e", argv[z]) == 0) {
			no_status_msg = true;
		} else if (strcmp("-S", argv[z]) == 0) {
			summary = true;
		} else if (strcmp("-ao", argv[z]) == 0) {
			output_append = true;
		} else if (strcmp("-aO", argv[z]) == 0) {
			raw_output_append = true;
		} else if (strcmp("-O", argv[z]) == 0) {
			if (z + 1 < argc) raw_output_file = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-c", argv[z]) == 0) {
			colored = true;
		} else if (strcmp("-i", argv[z]) == 0) {
			case_insensitive = true;
		} else if (strcmp("-r", argv[z]) == 0) {
			if (z + 1 < argc) num_retry_ms = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-R", argv[z]) == 0) {
			if (z + 1 < argc) num_retry_gs = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-T", argv[z]) == 0) {
			if (z + 1 < argc) num_threads = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-N", argv[z]) == 0) {
			if (z + 1 < argc) lead_nl = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-tm", argv[z]) == 0) {
			if (z + 1 < argc) recv_timeout_ms = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-tg", argv[z]) == 0) {
			if (z + 1 < argc) recv_timeout_gs = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-p", argv[z]) == 0) {
			if (z + 1 < argc) player_expr = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-st", argv[z]) == 0) {
			if (z + 1 < argc) srvtype_expr = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-sn", argv[z]) == 0) {
			if (z + 1 < argc) srvname_expr = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-sa", argv[z]) == 0) {
			if (z + 1 < argc) srvaddr_expr = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-sm", argv[z]) == 0) {
			if (z + 1 < argc) srvmap_expr = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-sv", argv[z]) == 0) {
			if (z + 1 < argc) srvver_expr = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-se", argv[z]) == 0) {
			hide_empty_srv = true;
		} else if (strcmp("-sp", argv[z]) == 0) {
			srv_show_players = true;
		} else {
			fprintf(stderr, "argument mess: wtf is \"%s\" supposed to mean?!\n", argv[z]);
			return false;
		}
	}
	return true;
}

void usage(const char *a0, int ec)
{
	fprintf(stderr, "usage: %s [parameters]\n", a0);
	fprintf(stderr,
		"\t-v: increase verbosity\n"
		"\t-h: display this usage information statement\n"
		"\t-f: force rerequesting server list if received less than announced by msrv\n"
		"\t-c: enable bash color sequences\n"
		"\t-i: case-insensitive regexps\n"
		"\t-e: hide trailing statusmsg after output (e.g. 'end of player listing')\n"
		"\t-se: do not display empty servers\n"
		"\t-sp: also output players, for matched servers\n"
		"\t-S: print stats as last line\n"
		"\t-ao do not truncate output file, i.e. append to it\n"
		"\t-aO do not truncate raw output file, i.e. append to it\n"
		"\t-O FILE: print raw (unfiltered) data to file (default: don't print raw data)\n"
		"\t-o FILE: write output to file instead of stdout\n"
		"\t-m STRING: specify a comman seperated list of master servers\n"
		"\t-p REGEXP: output all players with name matching REGEXP (default: all)\n"
		"\t-l NUMBR: in combination with -sp, put NUMBER players on one line\n"
		"\t-N NUMBER: print as many newlines before outputting\n"
		"\t-r NUMBER: number of retries for unresponsive master servers\n"
		"\t-R NUMBER: number of retries for unresponsive game servers\n"
		"\t-T NUMBER: number of threads\n"
		"\t-tm NUMBER: read timeout for master servers\n"
		"\t-tg NUMBER: read timeout for game servers\n"
		"\t-st REGEXP: output all servers with type matching REGEXP (default: none)\n"
		"\t-sn REGEXP: output all servers with name matching REGEXP (default: none)\n"
		"\t-sa REGEXP: output all servers with addr (host:port) matching REGEXP (default: none)\n"
		"\t-sm REGEXP: output all servers with map matching REGEXP (default: none)\n"
		"\t-sv REGEXP: output all servers with version matching REGEXP (default: none)\n"
	);
	exit(ec);
}

int main(int argc, char **argv)
{
	pthread_t *threads;
	int *tids;
	std::set<u_int64_t> dup_kill;
	std::list<u_int64_t> tmplist;
	timeval tmstart, tmend, tgend;
	int sucm;
	if (!init(argc, argv)) usage(argv[0], EXIT_FAILURE);

	threads = (pthread_t*) malloc(sizeof(pthread_t) * num_threads);
	tids = (int*) malloc(sizeof(int) * num_threads);
	for (int z = 0; z < num_threads; ++z)
		tids[z] = z;

	gettimeofday(&tmstart, NULL);
	request_serverlists(&tmplist, &sucm);
	gettimeofday(&tmend, NULL);
	for (std::list<u_int64_t>::const_iterator it = tmplist.begin(); it != tmplist.end(); ++it) {
		if (dup_kill.count(*it)) continue;
		dup_kill.insert(*it);
		list_work.push_back(new Server(*it));
	}

	if (verbosity >= 1) fprintf(stderr, "got %i servers from master servers\n", list_work.size());

	for (int z = 0; z < num_threads; ++z)
		if (pthread_create(&threads[z], NULL, process_queue, &tids[z]) != 0) fprintf(stderr, "failed to spawn thread %i\n", z);
	for (int z = 0; z < num_threads; ++z)
		if (pthread_join(threads[z], NULL) != 0) fprintf(stderr, "failed to join thread %i\n", z);

	gettimeofday(&tgend, NULL);

	free(threads);free(tids);

	if (verbosity >= 1) {
		fprintf(stderr, "\n");
		if (list_fail.size() > 0) fprintf(stderr, "%i servers failed to respond\n", list_fail.size());
	}

	int pcount = 0;
	if (raw_output_file || summary)
		for (std::list<Server*>::const_iterator it = list_done.begin(); it != list_done.end(); ++it)
			pcount += (*it)->pmap().size();

	if (raw_output_file && !(str_raw = fopen(raw_output_file, raw_output_append ? "a" : "w"))) {
		perror(raw_output_file);
		fprintf(stderr, "raw output will go to nowhere\n");
	}

	if (str_raw) {
		u_int64_t tstart = ((u_int64_t) (tmstart.tv_sec)) * 1000 + tmstart.tv_usec / 1000;
		unsigned int mdur = (int) ((((u_int64_t) (tmend.tv_sec)) * 1000 + tmend.tv_usec / 1000) - tstart);
		unsigned int gdur = (int) ((((u_int64_t) (tgend.tv_sec)) * 1000 + tgend.tv_usec / 1000) - (tstart + mdur));
		fprintf(str_raw, "D;%llu;%i;%i;%i;%i;%u;%u\n", tstart, sucm, pcount, list_done.size(), list_fail.size(), mdur, gdur);
	}

	if (!str_out && !(str_out = fopen(output_file, output_append ? "a" : "w"))) {
		perror(output_file);
		fprintf(stderr, "output will go to nowhere\n");
	}

	output_servers();
	output_players();

	if (str_out) {
		if (summary) fprintf(str_out, "%i players on %i servers (%i missed)\n", pcount, list_done.size(), list_fail.size());
		fclose(str_out);
	}
	if (str_raw) fclose(str_raw);

	/* just for teh lolz */
	for(std::list<Server *>::const_iterator it=list_done.begin(); it != list_done.end(); ++it) delete *it;
	for(std::list<Server *>::const_iterator it=list_fail.begin(); it != list_fail.end(); ++it) delete *it;

	return EXIT_SUCCESS;
}
