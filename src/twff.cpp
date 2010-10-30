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

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cerrno>

#include "bailsock.h"
#include "Server.h"

#define DEFAULT_MASTERPORT 8300

std::list<class Server*> list_work;
std::list<class Server*> list_done;
std::list<class Server*> list_fail;

std::list<const char*> list_masters;

pthread_mutex_t mutex_work;
FILE *str_out                    = stdout;

int recv_timeout_ms              = 3;
int recv_timeout_gs              = 3;
int num_threads                  = 20;
int verbosity                    = 0;
int num_retry_gs                 = 2;
int num_retry_ms                 = 2;
int lead_nl                      = 0;
int players_per_line             = 1;
bool no_status_msg               = false;
bool force_master_complete       = false;
bool colored                     = false;
bool case_insensitive            = false;
bool hide_empty_srv              = false;
bool srv_show_players            = false;
const char *output_file          = "-";

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

void output_servers();
void output_players();

void request_serverlists(std::list<u_int64_t> *destination);
int request_serverlist(std::list<u_int64_t> *destination, int maxCount, const char *masterHost, u_int16_t masterPort, int numRetry,bool forceComplete);
int perform_req_srvlist(std::list<u_int64_t> *destination, const char *masterHost, u_int16_t masterPort, int *numexp);
bool request_details(class Server *srv, int numRetry);
bool perform_req_details(class Server *srv);

int construct_packet(unsigned char *buffer, const unsigned char *pkt, size_t len);
int construct_getlist(unsigned char *buffer);
int construct_getinfo(unsigned char *buffer);
int construct_getcount(unsigned char *buffer);

void *process_queue(void*arg);
int oprintf(const char *format, ...);

bool init(int argc, char **argv);
bool process_args(int argc, char **argv);
void setup_defaults();
void usage(const char *a0, int ec) __attribute__ ((noreturn));
int main(int argc, char **argv);

void output_servers()
{
    if (!srvtype_expr && !srvmap_expr && !srvname_expr && !srvver_expr && !srvaddr_expr) return;

    static char addrbuf[22];
    bool show, shown_one = false;

    regex_t *type_regex = srvtype_expr ? new regex_t : NULL;
    regex_t *name_regex = srvname_expr ? new regex_t : NULL;
    regex_t *addr_regex = srvaddr_expr ? new regex_t : NULL;
    regex_t *map_regex  = srvmap_expr  ? new regex_t : NULL;
    regex_t *ver_regex  = srvver_expr  ? new regex_t : NULL;

    if (type_regex && regcomp(type_regex, srvtype_expr, REG_EXTENDED|(case_insensitive?REG_ICASE:0)) != 0)
        {fprintf(stderr, "could not compile gametype regexp, ignoring\n");delete type_regex;type_regex = NULL;}
    if (name_regex && regcomp(name_regex, srvname_expr, REG_EXTENDED|(case_insensitive?REG_ICASE:0)) != 0)
        {fprintf(stderr, "could not compile srvname regexp, ignoring\n");delete name_regex;name_regex = NULL;}
    if (addr_regex && regcomp(addr_regex, srvaddr_expr, REG_EXTENDED|(case_insensitive?REG_ICASE:0)) != 0)
        {fprintf(stderr, "could not compile addr regexp, ignoring\n");delete addr_regex;addr_regex = NULL;}
    if (map_regex && regcomp(map_regex, srvmap_expr, REG_EXTENDED|(case_insensitive?REG_ICASE:0)) != 0)
        {fprintf(stderr, "could not compile map regexp, ignoring\n");delete map_regex;map_regex = NULL;}
    if (ver_regex && regcomp(ver_regex, srvver_expr, REG_EXTENDED|(case_insensitive?REG_ICASE:0)) != 0)
        {fprintf(stderr, "could not version regexp, ignoring\n");delete ver_regex;ver_regex = NULL;}

    for (std::list<Server*>::const_iterator it = list_done.begin(); it != list_done.end(); ++it) {
        show = false;
        Server *srv = *it;
        if (type_regex && regexec(type_regex, srv->getGameType(), 0, NULL, 0) == 0) show = true;
        if (!show && name_regex && regexec(name_regex, srv->getName(), 0, NULL, 0) == 0) show = true;
        if (!show && addr_regex) {
            sprintf(addrbuf, "%s:%i", srv->getAddrStr(), srv->getPort());
            if (regexec(addr_regex, addrbuf, 0, NULL, 0) == 0) show = true;
        }
        if (!show && map_regex && regexec(map_regex, srv->getMap(), 0, NULL, 0) == 0) show = true;
        if (!show && ver_regex && regexec(ver_regex, srv->getVersion(), 0, NULL, 0) == 0) show = true;

        if (show && (!hide_empty_srv || srv->pmap().size() > 0)) {
            if (!shown_one) while(lead_nl-- > 0) oprintf("\n");
            shown_one = true;
            oprintf("\"%s%s%s\" - %s%s - %s%s (%s%i/%i%s) - %s%s:%i%s - %s%s%s - [%s%x;%i%s]\n",
                colored?"\033[01;32m":"", srv->getTrimmedName(),                      colored?"\033[0m":"",
                colored?"\033[01;31m":"", srv->getGameType(), srv->getMap(),          colored?"\033[0m":"",
                colored?"\033[01;36m":"", srv->getNumPlayers(), srv->getMaxPlayers(), colored?"\033[0m":"",
                colored?"\033[01;34m":"", srv->getAddrStr(), srv->getPort(),          colored?"\033[0m":"",
                colored?"\033[01;35m":"", srv->getVersion(),                          colored?"\033[0m":"",
                colored?"\033[01;37m":"",srv->getFlags(), srv->getProgress(),         colored?"\033[0m":"");
                /*TODO: write semantic colorizer to get rid of this crap*/


            if (srv_show_players) {
                int lc=0;
                for (std::set<Player*>::const_iterator itt = srv->pmap().begin(); itt != srv->pmap().end(); ++itt) {
                    oprintf("%s\"%s%s%s\"",lc==0?"\t":"; ", (colored?"\033[01;33m":""), (*itt)->getName(),colored?"\033[0m":"");
                    if ((++lc) == players_per_line) {
                        oprintf("\n");
                        lc=0;
                    }
                }
                if (lc) oprintf("\n");
            }
        }
    }

    if (!no_status_msg) {
        if (shown_one) oprintf("--- end of server listing ---\n");
        else           oprintf("--- no server matched ---\n");
    }

    if(type_regex)  {regfree(type_regex);delete type_regex;}
    if (name_regex) {regfree(name_regex);delete name_regex;}
    if (addr_regex) {regfree(addr_regex);delete addr_regex;}
    if (map_regex)  {regfree(map_regex); delete map_regex;}
    if (ver_regex)  {regfree(ver_regex); delete ver_regex;}
}

void output_players()
{
    if (!player_expr || strlen(player_expr) == 0) return;

    bool shown_one = false;
    regex_t *player_regex = player_expr ? new regex_t : NULL;

    if (player_regex && regcomp(player_regex, player_expr, REG_EXTENDED|(case_insensitive?REG_ICASE:0)) == 0) {
        for (std::list<Server*>::const_iterator it_srv = list_done.begin(); it_srv != list_done.end(); ++it_srv) {
            Server *srv = *it_srv;
            for (std::set<Player*>::const_iterator it_pl = srv->pmap().begin(); it_pl != srv->pmap().end(); ++it_pl) {
                if (regexec(player_regex, (*it_pl)->getName(), 0, NULL, 0) == 0) {
                    if (!shown_one) while(lead_nl-- > 0) oprintf("\n");
                    shown_one = true;
                    oprintf("\"%s%s%s\" is on %s%s:%i%s - %s%s - %s%s (%s%i/%i%s) - \"%s%s%s\"\n",
                        colored?"\033[01;33m":"", (*it_pl)->getName(),                        colored?"\033[0m":"",
                        colored?"\033[01;34m":"", srv->getAddrStr(), srv->getPort(),          colored?"\033[0m": "",
                        colored?"\033[01;31m":"", srv->getGameType(), srv->getMap(),          colored?"\033[0m":"",
                        colored?"\033[01;36m":"", srv->getNumPlayers(), srv->getMaxPlayers(), colored?"\033[0m":"",
                        colored?"\033[01;32m":"", srv->getTrimmedName(),                      colored?"\033[0m":"");
                        /*TODO: write semantic colorizer to get rid of this crap*/
                }
            }
        }
        if (!no_status_msg) {
            if (shown_one) oprintf("--- end of players listing ---\n");
            else           oprintf("--- no players matched ---\n");
        }
    } else fprintf(stderr, "could not compile player name regexp, cant continue\n");
    delete player_regex;
}

void request_serverlists(std::list<u_int64_t> *destination)
{
    char *host, *tmp;
    int port;
    for (std::list<const char *>::const_iterator it = list_masters.begin(); it != list_masters.end(); ++it) {
        tmp = strdup(*it);
        if (strchr(tmp, ':')) {
            host = strdup(strtok(tmp, ":"));
            port = strtol(tmp + strlen(host) + 1, NULL, 10);
            free(tmp);
        } else {
            port = DEFAULT_MASTERPORT;
            host = tmp;
        }
        request_serverlist(destination, -1, host, (u_int16_t) port, num_retry_ms, force_master_complete);
        free(host);
    }
}

int request_serverlist(std::list<u_int64_t> *destination, int maxCount, const char *masterHost, u_int16_t masterPort, int numRetry, bool forceComplete)
{
    if (!destination || !masterHost || !masterPort) return -1;
    if (!maxCount)
        {destination->clear();return 0;}

    std::list<u_int64_t> tmplist;
    int numsrv;
    int numsrv_announced;

    for (int trynum = 0; numRetry < 0 || trynum <= numRetry; ++trynum) {
        tmplist.clear();
        numsrv = perform_req_srvlist(&tmplist, masterHost, masterPort, &numsrv_announced);
        if (numsrv <= 0 || (forceComplete && (numsrv < numsrv_announced))) {
            if (verbosity >= 1) {
                fprintf(stderr, "while fetching serverlist from %s:%i (try %i): ", masterHost, masterPort, trynum);
                if (numsrv < 0) fprintf(stderr, "failed to contact server\n");
                else            fprintf(stderr, "got only %i out of %i announced servers\n", numsrv, numsrv_announced);
            }
            continue;
        }
        if (verbosity >= 1)
            fprintf(stderr, "retrieved %i out of %i announced servers from %s:%i (try %i):\n", numsrv, numsrv_announced, masterHost,masterPort, trynum);
        break;
    }
    for (std::list<u_int64_t>::const_iterator it = tmplist.begin(); it != tmplist.end(); ++it)
        destination->push_back(*it);
    return numsrv;
}

int perform_req_srvlist(std::list<u_int64_t> *destination, const char *masterHost, u_int16_t masterPort, int *numexp)
{
    static unsigned char iobuf[1024];
    socklen_t scklen = sizeof(struct sockaddr_in);
    struct sockaddr_in *server;
    int plen, num_bytes, sock, srv_count, srv_got;
    u_int32_t srv_addr;u_int16_t srv_port;u_int64_t srv_both;
    sock = bailsocket(AF_INET, SOCK_DGRAM, 0, recv_timeout_ms);
    if (!((server = bailmkaddr(masterHost, masterPort))))
        {close(sock);return -1;}

    plen = construct_getcount(iobuf);
    num_bytes = bailsendto(sock, iobuf, plen, 0, (const sockaddr*) server, scklen);
    num_bytes = bailrecvfrom(sock, iobuf, sizeof(iobuf), 0, NULL, NULL);
    if (num_bytes == 0)
        {close(sock);delete server;return -1;}

    srv_count = (iobuf[14] << 8) + iobuf[15];
    plen = construct_getlist(iobuf);
    num_bytes = bailsendto(sock, iobuf, plen, 0, (const sockaddr*) server, scklen);
    srv_got = 0;
    while (srv_got < srv_count) {
        num_bytes = bailrecvfrom(sock, iobuf, sizeof(iobuf), 0, NULL, NULL);
        if (num_bytes == 0) break;
        for (int z = 6 + P_LIST_LEN; z < num_bytes; z += 6) {
            srv_addr = (iobuf[z] << 24) + (iobuf[z + 1] << 16) + (iobuf[z + 2] << 8) + (iobuf[z + 3]);
            srv_port = (u_int16_t) ((iobuf[z + 5] << 8) + (iobuf[z + 4]));
            srv_both = (((u_int64_t) srv_addr) << 16) | srv_port;
            destination->push_back(srv_both);
        }
        srv_got += (num_bytes - (6 + P_LIST_LEN)) / 6;
        if (verbosity >= 1) fprintf(stderr, "\r%i/%i          ", srv_got, srv_count);
    }
    if (srv_got > 0 && verbosity >= 1) fprintf(stderr, "\n");
    if (numexp) *numexp = srv_count;
    close(sock);
    delete server;
    return srv_got;
}
bool request_details(class Server *srv, int numRetry)
{
    if (!srv) return false;

    std::list<u_int64_t> tmplist;
    int trynum;
    for (trynum = 0; numRetry < 0 || trynum <= numRetry; ++trynum) {
        if (!perform_req_details(srv)) {
            if (verbosity >= 1) fprintf(stderr, "could not fetch details of server %s:%i (try %i)\n", srv->getAddrStr(), srv->getPort(), trynum);
        } else return true;
    }
    if (verbosity >= 1) fprintf(stderr, "giving up on details of server %s:%i\n", srv->getAddrStr(), srv->getPort());
    return false;
}

bool perform_req_details(class Server *srv)
{
    static unsigned char iobuf[1024];
    socklen_t scklen = sizeof(struct sockaddr_in);
    struct sockaddr_in *server;
    int plen, num_bytes, sock, bytes_done;
    char *name_str, *score_str;

    sock = bailsocket(AF_INET, SOCK_DGRAM, 0, recv_timeout_gs);
    if (!((server = bailmkaddr(srv->getAddrStr(), srv->getPort()))))
        {close(sock);return -1;}

    plen = construct_getinfo(iobuf);
    num_bytes = bailsendto(sock, iobuf, plen, 0, (const sockaddr*) server, scklen);
    num_bytes = bailrecvfrom(sock, iobuf, sizeof(iobuf) - 1, 0, NULL, NULL);
    if (num_bytes == 0)
        {close(sock);delete server;return false;}

    iobuf[sizeof(iobuf) - 1] = iobuf[num_bytes] = '\0';
    /*TODO: do this properly*/
    char *ptr = ((char*) iobuf) + 6 + P_INFO_LEN;
    srv->setVersion(ptr);
    srv->setName(ptr += strlen(ptr) + 1);
    srv->setMap(ptr += strlen(ptr) + 1);
    srv->setGameType(ptr += strlen(ptr) + 1);
    srv->setFlags((int) strtol(ptr += strlen(ptr) + 1, NULL, 10));
    srv->setProgress((int) strtol(ptr += strlen(ptr) + 1, NULL, 10));
    srv->setNumPlayers((int) strtol(ptr += strlen(ptr) + 1, NULL, 10));
    srv->setMaxPlayers((int) strtol(ptr += strlen(ptr) + 1, NULL, 10));
    ptr += strlen(ptr) + 1;
    bytes_done = ptr - ((char*) iobuf);
    while (bytes_done < num_bytes) {
        name_str = ptr;
        ptr += strlen(ptr) + 1;
        bytes_done += strlen(name_str) + 1;
        if (bytes_done >= num_bytes) break/*skip last string which some ddrace servers seem to append*/;
        score_str = ptr;
        ptr += strlen(ptr) + 1;
        bytes_done += strlen(score_str) + 1;
        srv->addPlayer(name_str, (int) strtol(score_str, NULL, 10));
    }
    close(sock);
    delete server;
    return true;
}

int construct_packet(unsigned char *buffer, const unsigned char *pkt, size_t len)
{
    for (int z = 0; z < 6; ++z)
        buffer[z] = 0xff;
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
    if (arg) {
    }//skip warning
    class Server *srv;
    for (;;) {
        srv = NULL;
        pthread_mutex_lock(&mutex_work);
        if (!list_work.empty()) {
            srv = list_work.front();
            list_work.pop_front();
            if (verbosity >= 1)    fprintf(stderr, "\r%i servers remaining            ", list_work.size());
        } else if (verbosity >= 1) fprintf(stderr, "\rpending....                     ");
        pthread_mutex_unlock(&mutex_work);
        if (!srv) break;
        if (request_details(srv, num_retry_gs)) {
            pthread_mutex_lock(&mutex_work);
            list_done.push_back(srv);
            pthread_mutex_unlock(&mutex_work);
        } else {
            pthread_mutex_lock(&mutex_work);
            list_fail.push_back(srv);
            pthread_mutex_unlock(&mutex_work);
        }
    }
    return 0;
}

int oprintf(const char *format, ...)
{
    static bool trouble = false;
    if (trouble) return 0;
    if (!str_out) {
        if (!((str_out = fopen(output_file, "w")))) {
            perror(output_file);
            trouble = true;
            fprintf(stderr, "output will go to nowhere\n");
        }
    }
    if (trouble) return 0;
    va_list l;
    va_start(l,format);
    int r = vfprintf(str_out, format, l);
    va_end(l);
    return r;
}

bool init(int argc, char **argv)
{
    if (!process_args(argc, argv)) return false;
    if (strcmp("-", output_file) != 0) str_out = NULL;//lazy fopen
    pthread_mutex_init(&mutex_work, NULL);
    if (list_masters.empty()) {
        list_masters.push_back("master1.teeworlds.com");
        list_masters.push_back("master2.teeworlds.com");
        list_masters.push_back("master3.teeworlds.com");
        list_masters.push_back("master4.teeworlds.com");
    }
    return true;
}

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
                do {
                    list_masters.push_back(strdup(ptr));
                } while ((ptr = strtok(NULL, ",")));
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
    std::set<u_int64_t> dup_kill;
    std::list<u_int64_t> tmplist;

    if (!init(argc, argv)) usage(argv[0], EXIT_FAILURE);

    threads = (pthread_t*) malloc(sizeof(pthread_t) * num_threads);

    request_serverlists(&tmplist);
    for (std::list<u_int64_t>::const_iterator it = tmplist.begin(); it != tmplist.end(); ++it) {
        if (dup_kill.count(*it)) continue;
        dup_kill.insert(*it);
        list_work.push_back(new Server(*it));
    }

    if (verbosity >= 1) fprintf(stderr, "got %i servers from master servers\n", list_work.size());

    for (int z = 0; z < num_threads; ++z)
        if (pthread_create(&threads[z], NULL, process_queue, NULL) != 0) fprintf(stderr, "failed to spawn thread %i\n", z);
    for (int z = 0; z < num_threads; ++z)
        if (pthread_join(threads[z], NULL) != 0) fprintf(stderr, "failed to join thread %i\n", z);

    free(threads);

    if (verbosity >= 1) {
        fprintf(stderr, "\n");
        if (list_fail.size() > 0) fprintf(stderr, "%i servers failed to respond\n", list_fail.size());
    }

    output_servers();
    output_players();

    if (str_out) fclose(str_out);

    return EXIT_SUCCESS;
}
