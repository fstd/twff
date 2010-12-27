/*
 * twff.cpp
 *
 *  Created on: Oct 30, 2010
 *      Author: fisted
 */

/* ************************************************************************** */
/* *                                 includes                                 */
/* ************************************************************************** */

#include <set>
#include <map>
#include <list>
#include <string>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cerrno>

#include <netinet/in.h>
#include <pthread.h>
#include <regex.h>
#include <sys/time.h>

#include "bailsock.h"
#include "Server.h"



/* ************************************************************************** */
/* *                                 defines                                  */
/* ************************************************************************** */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define COLOR_OVERHEAD       12 /* number of chars a bash color sequence and its reset sequence need */
#define DEFAULT_MASTERPORT 8300 /* make a guess */
#define DATA_OFFSET          14 /* the offset at which actual data starts, in packets from master/game servers*/
#define LISTBUF_SZ         8192 /* buffer size for receiving master server data */
#define INFOBUF_SZ         1024 /* buffer size for receiving game server data */
#define OUT_MAX_LINE_LEN    512 /* the maximum length an output line can take, only affects regular output */

/* bash color codes for colored mode TODO make adjustable */
#define CO_ADR             1,37 /* server address */
#define CO_TYP             1,36 /* server game type */
#define CO_MAP             1,34 /* server map */
#define CO_NPL             1,36 /* server num players/max players */
#define CO_SNM             1,32 /* server name */
#define CO_ARR             1,30 /* arrow-like line pointing from player to server */
#define CO_POS             1,31 /* player name while displaying players from matched server */
#define CO_PNM             1,31 /* player name while displaying matched players  */
#define CO_FRM             0,33 /* color of the frame we might draw around output */

/*for error/diagnostic/verbose output*/
#define DBG(LVL,ARG...) do{ if (g_str_err && g_verb >= (LVL)) fprintf(g_str_err,ARG); } while(0)



/* ************************************************************************** */
/* *                            general globals                               */
/* ************************************************************************** */

/* this is where our threads operate on, ... */
std::list<Server*> g_list_work;
/* ...eventually putting processed servers into one of these: */
std::list<Server*> g_list_done;
std::list<Server*> g_list_fail;

/* master server list, elements are either host or ip, or host or ip followed by :port */
std::list<const char*> g_list_masters;

FILE *g_str_err = stderr; /* we will write status/error/diagnostic/verbose output to this */
FILE *g_str_out = stdout; /* regular output goes here */
FILE *g_str_raw = NULL;   /* raw output goes here */
FILE *g_str_in  = NULL;   /* if not null, we read a raw log from here */

std::map<int,char*> g_color_map;    /* remembers color sequences to not regenerate them every time */
pthread_mutex_t g_mutex_work;       /* the mutex we lock on when accessing the work queue */
pthread_mutex_t g_mutex_err;        /* just in case we want synchronized err output */
bool g_err_dirty = false;           /* true when the last printed char to err_str is not a newline */

regex_t *g_crx_adr = NULL;          /* compiled regular expression for server addr:port */
regex_t *g_crx_snm = NULL;          /* compiled regular expression for server name */
regex_t *g_crx_typ = NULL;          /* compiled regular expression for server game type */
regex_t *g_crx_map = NULL;          /* compiled regular expression for server map */
regex_t *g_crx_ver = NULL;          /* compiled regular expression for server version */
regex_t *g_crx_pnm = NULL;          /* compiled regular expression for player name */

/* these hold the lengths of the longest strings of each displayed attribute, for proper formatting */
int g_maxlen_s_adr = 0;
int g_maxlen_s_snm = 0;
int g_maxlen_s_typ = 0;
int g_maxlen_s_map = 0;
int g_maxlen_s_pnm = 0;
int g_maxlen_s_ver = 0;

int g_maxlen_p_ver = 0;
int g_maxlen_p_adr = 0;
int g_maxlen_p_snm = 0;
int g_maxlen_p_typ = 0;
int g_maxlen_p_map = 0;
int g_maxlen_p_pnm = 0;



/* ************************************************************************** */
/* *                         configuration globals                            */
/* ************************************************************************** */

int g_recv_timeout_ms     =  3;    /* timeout for master server requests, in seconds */
int g_recv_timeout_gs     =  3;    /* timeout for game server requests, in seconds */
int g_num_threads         = 20;    /* number of threads we're going to use */
int g_verb                =  0;    /* level of verbosity (towards g_str_err) */
int g_gs_retry            =  2;    /* number of retries for unresponsive game servers */
int g_ms_retry            =  2;    /* number of retries for unresponsive master servers */
int g_lead_nl             =  0;    /* number of newlines to print out before any output has been done */
int g_players_per_line    =  1;    /* number of players to fit on a line, when displ. players of matched srvs */
int g_svl_tol             =  0;    /* number of missing servers in server list we're going to tolerate */
int g_progress_bar        =  0;    /* width of progress bar, uncool when verbose. 0 = off */

bool g_summary            = false; /* output a trailing summary line about containing some stats */
bool g_output_append      = false; /* append to output stream/file, instead of truncating it first */
bool g_error_append       = false; /* append to error stream/file, instead of truncating it first */
bool g_raw_output_append  = false; /* append to raw output stream/file, instead of truncating it first */
bool g_colored            = false; /* use color or not */
bool g_display_frame      = false; /* draw a cute frame around or output */
bool g_display_seps       = false; /* draw separating lines between players/servers */
bool g_case_insensitive   = false; /* perform case insensitive regex matching */
bool g_no_match_info      = false; /* print info that no server or player matched, rather than being silent */
bool g_no_empty_sv        = false; /* never match empty servers */
bool g_sv_show_players    = false; /* for matched server, display their players */
bool g_strict_sanitize    = true;  /* replace non-ascii characters (> 0x7f) with g_sane_char_strict */
bool g_global_palign      = false; /* globally align players in coloums when displ. players of matched srvs */
bool g_skip_last_nl       = false; /* skip the very last newline */
char g_frame_char         = '*';   /* the character we want to draw our frame with */
char g_sane_char          = ' ';   /* ascii control chars will be replaced with this */
char g_sane_char_strict   = '_';   /* when strict sanitizing, chars > 0x7f will be replaced with this */

const char *g_output_file = "-";   /* filename of where reg output should go to, '-' = stdout, NULL = disabled */
char *g_raw_file          = NULL;  /* filename of where raw output should go to, '-' = stdout, NULL = disabled */
const char *g_input_file  = NULL;  /* filename of where reg output should go to, '-' = stdout, NULL = disabled */

const char *g_rexp_p_pnm  = ".*";  /* player name matching expression */
const char *g_rexp_s_typ  = NULL;  /* server game type matching expression */
const char *g_rexp_s_snm  = NULL;  /* server name matching expression */
const char *g_rexp_s_adr  = NULL;  /* server addr:port matching expression */
const char *g_rexp_s_map  = NULL;  /* server map matching expression */
const char *g_rexp_s_ver  = NULL;  /* server version matching expression */

/* these reflect the maximum field widths for each field, when outputting
 * the _s_ variables hold field width limits for server output, the _p_
 * for player output, respectively. */
int g_fw_max_s_snm = 1023;
int g_fw_max_s_typ = 1023;
int g_fw_max_s_adr = 1023;
int g_fw_max_s_map = 1023;
int g_fw_max_s_pnm = 1023;

int g_fw_min_s_snm = 0;
int g_fw_min_s_adr = 0;
int g_fw_min_s_typ = 0;
int g_fw_min_s_map = 0;
int g_fw_min_s_pnm = 0;

int g_fw_max_p_pnm = 1023;
int g_fw_max_p_typ = 1023;
int g_fw_max_p_adr = 1023;
int g_fw_max_p_map = 1023;
int g_fw_max_p_snm = 1023;

int g_fw_min_p_snm = 0;
int g_fw_min_p_typ = 0;
int g_fw_min_p_adr = 0;
int g_fw_min_p_map = 0;
int g_fw_min_p_pnm = 0;



/* ************************************************************************** */
/* *                             protocol data                                */
/* ************************************************************************** */

const unsigned char P_GETCOUNT[] = {0xff,0xff,0xff,0xff,0x63,0x6f,0x75,0x6e};
const unsigned char P_GETLIST[]  = {0xff,0xff,0xff,0xff,0x72,0x65,0x71,0x74};
const unsigned char P_GETINFO[]  = {0xff,0xff,0xff,0xff,0x67,0x69,0x65,0x66};
const int P_INFO_LEN             = 8;
const int P_LIST_LEN             = 8;



/* ************************************************************************** */
/* *                            function headers                              */
/* ************************************************************************** */


/* calls acquire_list for every master server we know about, appending servers
 * to `dest`. stores the number of successfully queried master servers in
 * nSuc, if not null */
void acquire_all_lists(std::list<u_int64_t> *dest, int *nSuc);

/* (re-)tries at most `numRetry` times to get a server list from a particular master
 * server given through `mHost` and `mPort` by calling fetch_list.
 * the actual number of attempts will be `numRetry` + 1, because the very first
 * attempt is not considered a 're'-try. if `numRetry` is < 0, we try until
 * we succeed
 * a list is considered valid when its size is not more than `tol` servers
 * short of what was announced by the master servers.
 * if `tol` is negative, any server list will be accepted
 * if out of attempts due to short server lists, the largest list so far
 * received is chosen, even though its short. thus `tol` is not a hard
 * restriction but rather a hint
 * servers are appended to `dest`, i.e. it is not purged before.
 * returns the number of server*/
int acquire_list(std::list<u_int64_t> *dest, const char *mHost, u_int16_t mPort, int maxRetry, int tol);

/* performs the actual UDP requests to query a server list from a master server
 * given through `mHost` and `mPort`. Due to the unrelieable nature of UDP,
 * and as an implication of the way the master servers operate, the number of
 * servers received may not match the number announced by a server.
 * if `nAnn` is not null, the number of announced servers is stored there.
 * servers are appended to `dest`, i.e. it is not purged before.
 * returns the number of servers actually received (as opposed to announced) */
int fetch_list(std::list<u_int64_t> *dest, const char *mHost, u_int16_t mPort, int *nAnn);

/* (re-)tries at most `numRetry` times to get details of a game server by calling
 * fetch_details for that server
 * the actual number of attempts will be `numRetry` + 1, because the very first
 * attempt is not considered a 're'-try. if `numRetry` is < 0, we try until
 * we succeed
 * returns true on success */
bool acquire_details(Server *sv, int maxRetry);

/* performs the actual UDP request to query details of a game server
 * on success, `sv` gets its attributes set to the received values and true is
 * returned */
bool fetch_details(Server *sv);

/* construct the necessary packets we need to send
 * returns the length of the packet data written to `buf` */
int make_paket(unsigned char *buf, const unsigned char *pkt, size_t len);

/* getcount is sent to query a master server for its amount of known servers*/
int mkpk_getcount(unsigned char *buf);

/* getlist is sent to query a master server for a server list */
int mkpk_getlist(unsigned char *buf);

/* getinfo is sent to query a game server for server details */
int mkpk_getinfo(unsigned char *buf);

/* this will be provided to the worker threads, as their function to be run.
 * it pops a server off the to-be-investigated queue, calls aquire_details on
 * it, eventually adding it to the list of processed, or the list of failed
 * servers. this process loops until the queue is empty, which makes this
 * function terminate. the return value is unused. */
void *process_queue(void*arg);

/* match all players in `plist` against the player matching criteria,
 * append matching players to `dest` (which is not purged before)
 * returns the number of matched players. */
int match_players(std::list<Player*> *dest, std::list<Server*> *slist);

/* eat a raw log (see -O switch) and re-process everything as if we had
 * just been querying the master and game servers
 * for completeness, the number of failed game servers (when the raw log
 * was created) is stored in `num_failed_gs`
 * returns true on success */
bool eat_raw(FILE *str, int *num_failed_gs);

/* match all servers in `slist` against the server matching criteria,
 * append matching servers to `dest` (which is not purged before)
 * returns the number of matched servers. */
int match_servers(std::list<Server*> *dest, std::list<Server*> *slist);

/* output easy to parse raw data */
void output_raw(std::list<Server*> *slist);

/* outputs a list of servers (i.e. the list of matched servers) and potentially
 * its players, with some decent formatting */
void output_servers(std::list<Server*> *slist);

/* outputs a list of players (i.e. the list of matched players) and the servers
 * they play on, with some decent formatting */
void output_players(std::list<Player*> *plist);

/* return a bash color sequence with attribute `attr` and color `col`, or
 * an empty string, if we're in non-colored mode */
const char *get_color(int attr, int col);

/* return `v` if in interval [min,max], otherwise return min or max, whichever
 * is closer */
int clamp(int v, int min, int max);

/* investigate servers attributes to find the longest strings, enabling us
 * to do some nice formatting. parameters may be NULL, and will be ignored in
 * case they are. `ml_pnm` will represent the longest player name among all
 * every players on every server. */
void gather_s_format_hints(std::list<Server*> *slist, int *ml_adr, int *ml_snm, int *ml_typ,
		int *ml_map, int *ml_ver, int *ml_pnm);

/* do like above, for a player list
 * does not differ much from (and internally uses) gather_s_format_hints
 * except for that `ml_pnm` will represent the longest player name only among
 * the players in `plist`, obviously. the other values refer to the servers
 * the players in `plist` play on. */
void gather_p_format_hints(std::list<Player*> *plist, int *ml_adr, int *ml_snm, int *ml_typ,
		int *ml_map, int *ml_ver, int *ml_pnm);

/* test if player information (i.e. name and score) provided by a game server
 * are sane, i.e. non-null, non-empty and (for score) numeric. */
bool sane_player(const char *pnm, const char *sco);


/* test if server information (i.e. name, type, map, etc.) provided by a game
 * server are sane, i.e. non-null, non-empty and (for values supposed to be
 * numeric) numeric. */
bool sane_server(const char *ver, const char *snm, const char *map, const char *typ,
		       const char *flg, const char *prg, const char *npl, const char *mpl);

/* trim non-printable ascii chars off both ends of a string
 * returns `s`, for convenience */
char *lrtrim(char *s);

/* sanitize a string by replacing ascii control characters with a sane char
 * (see global g_sane_char). depending on `strict`, it might replace other non-
 * ascii chars ( > 0x7f), too, with (global) strict_sane_char.
 * eventually, the string is whitespace-trimmed on both ends
 * using lrtrim()
 * returns `s`, for convenience */
char *sanitize(char *s, bool strict);

/* test if a string is numeric, i.e. optionally starts with a '+' or '-',
 * and then only, and at least one, digit. */
bool is_numeric(const char *s);

/* create a string hexdump from `s` in `dest`
 * to fit in the entire string, `dest` must be of a size of at least 3 times
 * the number of characters in `s` (not including the final '\0')*/
size_t hexdump(char *dest, const char *s, size_t bufsz);

/* create a string dump (hexdump + sanitized string) utilizing hexdump()
 * to fit in the entire string, `dest` must be of a size of at least 4 times
 * the number of characters in `s` (not including the final '\0') plus one. */
size_t dump_string(char *dest, const char *s, size_t bufsz);

/* dump any number of strings to stream/file str. just for convenience.
 * needs a NULL as its very last argument. creates dumps by using dump_string*/
void dump_strings(FILE *str, ...);

/* get a word from `data`, in host byte order */
u_int16_t getWord(const void* data);

/* get a word from `data` without any conversion */
u_int16_t getWordNC(const void* data);

/* get a dword from `data`, in host byte order */
u_int32_t getDWord(const void* data);

/* get a dword from `data` without any conversion */
u_int32_t getDWordNC(const void* data);

/* get a unix timestamp in milliseconds */
u_int64_t getMSTimestamp(const struct timeval *tv);

/* get a time interval in milliseconds */
u_int64_t getMSInterval(struct timeval *from, struct timeval *to);

/* initializes stuff like regular expressions, default master servers,
 * and call process_args */
bool init(int argc, char **argv);

/* this is a huge crapload and should be sanitized */
bool process_args(int argc, char **argv);

void usage(FILE *str, const char *a0, int ec) __attribute__ ((noreturn));

/* returns `linelen` g_frame_char's (potentially colored; followed by '\n')
 * remembers the last returned line, successive calls with `linelen` == 0
 * will return the last line again */
const char *get_frameline(int linelen);

/* non-critical yet useful for keeping memory analysis dumps clean */
void purge();

int main(int argc, char **argv);



/* ************************************************************************** */
/* *                       function of implementation                         */
/* ************************************************************************** */

void acquire_all_lists(std::list<u_int64_t> *dest, int *nSuc)
{
	char *mHost, *tmp;
	u_int16_t mPort;
	std::list<const char *>::const_iterator it_ms;

	if (nSuc) *nSuc = 0;

	for (it_ms = g_list_masters.begin(); it_ms != g_list_masters.end(); ++it_ms) {

		/* if :port provided, cut it off and parse, otherwise use default master port */
		mHost = tmp = strdup(*it_ms);
		if (strchr(tmp, ':')) {
			mHost = strdup(strtok(tmp, ":"));
			mPort = (u_int16_t) strtol(tmp + strlen(mHost) + 1, NULL, 10);
			free(tmp);
		} else
			mPort = DEFAULT_MASTERPORT;

		/* attempt to get an actual server list for the current master srv */
		if (acquire_list(dest, mHost, (u_int16_t)mPort, g_ms_retry, g_svl_tol) > 0)
			if (nSuc) ++(*nSuc);

		free(mHost);
	}
}

int acquire_list(std::list<u_int64_t> *dest, const char *host, u_int16_t port, int maxRetry, int tol)
{
	std::list<u_int64_t>::const_iterator it_addr;
	std::list<u_int64_t> tmplist, /* temporary list for storing the results of the current attempt */
	                     bestlist;/* keeps track of the largest server list we got so far */
	int numsv, numsv_an, /* number of servers returned (and number announced) for the current attempt*/
	    best_numsv = -1, best_numsv_an = 0, /* best values for those we got so far */
	    attempt; /* attempt counter */

	bool okay; /* received server list considered okay? */

	/* crap call */
	if (!dest || !host || !port)
		return -1;

	for (attempt = 0; maxRetry < 0 || attempt <= maxRetry; ++attempt) {
		tmplist.clear();
		numsv = fetch_list(&tmplist, host, port, &numsv_an);

		/* the list is considered valid when either tol is negative, or when
		 * it does not lack more than tol servers */
		okay = (numsv > 0 && (tol <= 0 || (numsv_an - numsv) < tol));

		/* valid or not, we save the list in case we run out of attempts
		 * without even one response fitting our `tol` requirements. */
		if (tmplist.size() > bestlist.size()) {
			best_numsv_an = numsv_an;
			best_numsv = numsv;
			bestlist.clear();
			for (it_addr = tmplist.begin(); it_addr != tmplist.end(); ++it_addr)
				bestlist.push_back(*it_addr);
		}

		if (okay) {
			++attempt;
			break;
		}
	}

	/* copy our best response into dest */
	for (it_addr = bestlist.begin(); it_addr != bestlist.end(); ++it_addr)
		dest->push_back(*it_addr);


	if (best_numsv < 0)
		DBG(1,"failed to retrieve any server from \"%s:%i\", giving up after %i attempts\n",
				host, port, attempt);
	else
		DBG(1,"retrieved %i out of %i servers from \"%s:%i\" in %i attempts\n",
				best_numsv, best_numsv_an, host, port, attempt);

	/* and return how many servers we got, might be -1 if we kept failing */
	return best_numsv;
}

int fetch_list(std::list<u_int64_t> *dest, const char *mHost, u_int16_t mPort, int *nAnn)
{
	static unsigned char iobuf[LISTBUF_SZ]; /* io action takes place here */
	struct sockaddr_in *saddr_srv = NULL;
	int sck,          /* the socket we're going to use */
	    datalen,      /* length of recv data */
	    offset,       /* keeps track of where we are while eating the server list*/
	    sv_count = 0, /* holds the number of servers the master server announced to have */
	    sv_got = 0,   /* counts the number of servers actually received */
	    ret = -1;     /* what are we going to return? (on success, the number of servers, i.e. sv_got) */

	sck = bailsocket(AF_INET, SOCK_DGRAM, 0, g_recv_timeout_ms);
	if (!(saddr_srv = bailmkaddr(mHost, mPort)))
		goto fl_bail_out;

	/* send 'get count' request asking for how many servers the master server can provide */
	bailsendto(sck, iobuf, mkpk_getcount(iobuf), 0, (const sockaddr*)saddr_srv, sizeof(struct sockaddr_in));

	if (bailrecvfrom(sck, iobuf, sizeof iobuf, 0, NULL, NULL) < DATA_OFFSET + 2)
		goto fl_bail_out;

	sv_count = getWord(iobuf + DATA_OFFSET);

	/* now send 'get list' request asking for the actual list */
	bailsendto(sck, iobuf, mkpk_getlist(iobuf), 0, (const sockaddr*)saddr_srv, sizeof(struct sockaddr_in));

	while (sv_got < sv_count) {
		/* we might receive multiple response packets */
		datalen = bailrecvfrom(sck, iobuf, sizeof iobuf, 0, NULL, NULL);
		if (datalen < DATA_OFFSET)
			break; /* recv timeout or too less data */

		/* walk over response, reading servers into the target list */
		for (offset = DATA_OFFSET; offset < datalen; offset += 6)
			dest->push_back((((u_int64_t)getDWord(iobuf + offset)) << 16)
					| getWordNC(iobuf + offset + 4));

		sv_got += (datalen - DATA_OFFSET) / 6; /* 6 bytes per entry */

		DBG(2,"\rgot %i/%i servers from %s:%i       ", sv_got, sv_count, mHost, mPort);
	}

	if (nAnn) *nAnn = sv_count;

	if (sv_got >= 0) DBG(2, "\n");

	ret = sv_got;

fl_bail_out:

	if (sck >= 0) close(sck);
	if (saddr_srv) delete saddr_srv;
	return ret;
}

bool acquire_details(Server *sv, int maxRetry)
{
	int attempt; /* attempt counter */

	if (!sv) return false;

	for (attempt = 0; maxRetry < 0 || attempt <= maxRetry; ++attempt)
		if (fetch_details(sv))
			return true;

	pthread_mutex_lock(&g_mutex_err);
	DBG(1,"%sfailed to inquire \"%s\" (giving up after %i attempts)\n", (g_err_dirty?"\n":""),
			sv->getHost(), attempt);
	g_err_dirty = false;
	pthread_mutex_unlock(&g_mutex_err);
	return false;
}

bool fetch_details(Server *sv)
{
	/* io buffer can't be static or global, we must be reentrant */
	unsigned char iobuf[INFOBUF_SZ];
	struct sockaddr_in *saddr_srv = NULL;

	int sck,      /* the socket fd we're going to use */
	    bcnt,      /* byte count; amount of data received by the server, without headers*/
	    len,       /* holds string lenghts while eating server data */
	    bdone = 0; /* bytes done; amount of bytes processed so far */

	char *ptr, /* keep track of our position, while eating server data */
	     /* those below hold data received by a game server, to be checked for sanity (and maybe sanitized)*/
	     *ver = NULL, *snm = NULL, *map = NULL, *typ = NULL, *pnm = NULL,
	     *flg = NULL, *prg = NULL, *npl = NULL, *mpl = NULL, *sco = NULL,
	     /* those below hold un-sanitized data for possible raw dump if not sane: */
	     *raw_snm = NULL, *raw_ver = NULL, *raw_typ = NULL, *raw_map = NULL, *raw_pnm = NULL;

	bool ret = false; /* what are we going to return? */

	sck = bailsocket(AF_INET, SOCK_DGRAM, 0, g_recv_timeout_gs);
	if (!(saddr_srv = bailmkaddr(sv->getIP(), sv->getPort())))
		goto fd_bail_out;

	/* send info request */
	bailsendto(sck, iobuf, mkpk_getinfo(iobuf), 0, (const sockaddr*)saddr_srv, sizeof (struct sockaddr_in));

	bcnt = bailrecvfrom(sck, iobuf, (sizeof iobuf) - 1, 0, NULL, NULL);
	if (bcnt <= DATA_OFFSET) /* obviously too less data, or recv timeout */
		goto fd_bail_out;

	/* make sure its zero terminated no matter what the server supplies */
	iobuf[bcnt] = '\0';

	/* make ptr point to the start of payload */
	ptr = ((char*) iobuf) + DATA_OFFSET;
	bcnt -= DATA_OFFSET;

	/* read data, bail out if data is short */
	ver = strdup(ptr);            if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) goto fd_bail_out;
	snm = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) goto fd_bail_out;
	map = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) goto fd_bail_out;
	typ = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) goto fd_bail_out;
	flg = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) goto fd_bail_out;
	prg = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) goto fd_bail_out;
	npl = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) goto fd_bail_out;
	mpl = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >  bcnt) goto fd_bail_out;

	/* in case we need the original (unsanitized) values */
	raw_snm = strdup(snm);
	raw_ver = strdup(ver);
	raw_typ = strdup(typ);
	raw_map = strdup(map);

	sanitize(ver, g_strict_sanitize);
	sanitize(snm, g_strict_sanitize);
	sanitize(map, g_strict_sanitize);
	sanitize(typ, g_strict_sanitize);

	if (!sane_server(ver, snm, map, typ, flg, prg, npl, mpl)) {
		pthread_mutex_lock(&g_mutex_err);
		DBG(1,"%scrap server \"%s\":\n", (g_err_dirty?"\n":""), sv->getHost());
		if (g_verb >= 1) dump_strings(g_str_err, raw_snm, raw_typ, raw_map, raw_ver,
					snm, typ, map, ver, flg, prg, npl, mpl, NULL);
		g_err_dirty = false;
		pthread_mutex_unlock(&g_mutex_err);
		goto fd_bail_out;
	}

	/* now read player list */
	while (bdone < bcnt) {

		/* we don't bail here, if at end of data, because some ddrace servers seem to append an extra
		 * string to the player list. we break, which makes us actually ignore that string. */
		pnm = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >= bcnt) break;
		sco = strdup(ptr += len + 1); if ((bdone += (len = strlen(ptr)) + 1) >  bcnt) goto fd_bail_out;

		raw_pnm = strdup(pnm);
		sanitize(pnm, g_strict_sanitize);

		/* test player sanity */
		if (!sane_player(pnm, sco)) {
			pthread_mutex_lock(&g_mutex_err);
			DBG(1,"%scrap player on \"%s\":\n", (g_err_dirty?"\n":""), sv->getHost());
			g_err_dirty = false;
			if (g_verb >= 1) dump_strings(g_str_err, raw_pnm, pnm, sco, NULL);
			pthread_mutex_unlock(&g_mutex_err);
			goto fd_bail_out;
		}

		/* everything okay, add to servers player set */
		sv->addPlayer(pnm, (int) strtol(sco, NULL, 10));

		free(pnm);free(sco);free(raw_pnm);
		pnm = raw_pnm = sco = NULL;
	}

	/* if we reach this, everything is sane and valid */
	sv->setVersion(ver);
	sv->setName(snm);
	sv->setMap(map);
	sv->setGameType(typ);
	sv->setFlags((int) strtol(flg, NULL, 10));
	sv->setProgress((int) strtol(prg, NULL, 10));
	sv->setNumPlayers((int) strtol(npl, NULL, 10));
	sv->setMaxPlayers((int) strtol(mpl, NULL, 10));

	ret = true;

fd_bail_out:

	if (sck >= 0) close(sck);

	/* free any memory we might have allocated */
	free(ver); free(snm); free(map); free(typ), free(flg); free(prg); free(npl); free(mpl);
	free(pnm); free(sco); free(raw_snm); free(raw_ver); free(raw_typ); free(raw_map); free(raw_pnm);

	if (saddr_srv) delete saddr_srv;

	if (!ret) sv->clearPlayers();

	return ret;
}

int match_servers(std::list<Server*> *dest, std::list<Server*> *slist)
{
	Server *sv;
	std::list<Server*>::const_iterator it_sv;
	size_t prevsize;

	/* crap call */
	if (!g_crx_typ && !g_crx_map && !g_crx_snm && !g_crx_ver && !g_crx_adr)
		return 0;

	prevsize = dest->size();

	for (it_sv = slist->begin(); it_sv != slist->end(); ++it_sv) {
		sv = *it_sv;

		/* if we dont want to match empty servers, we skip them here */
		if (g_no_empty_sv && sv->pmap().empty())
			continue;

		/* otherwise we try to find a match, adding it to dest if found */
		if (       (g_crx_typ && regexec(g_crx_typ, sv->getType(), 0, NULL, 0) == 0)
		        || (g_crx_snm && regexec(g_crx_snm, sv->getName(), 0, NULL, 0) == 0)
		        || (g_crx_map && regexec(g_crx_map, sv->getMap(),  0, NULL, 0) == 0)
		        || (g_crx_ver && regexec(g_crx_ver, sv->getVer(),  0, NULL, 0) == 0)
		        || (g_crx_adr && regexec(g_crx_adr, sv->getHost(), 0, NULL, 0) == 0))
			dest->push_back(sv);

	}

	/* return the number of matched servers */
	return dest->size() - prevsize;
}

int match_players(std::list<Player*> *dest, std::list<Server*> *slist)
{
	std::list<Server*>::const_iterator it_sv;
	std::set<Player*>::const_iterator it_pl;
	size_t prevsize;

	/* crap call */
	if (!g_crx_pnm)
		return 0;

	prevsize = dest->size();
	/* walk over all players on all servers, matching their names against g_cry_pnm */
	for (it_sv = slist->begin(); it_sv != slist->end(); ++it_sv)
		for (it_pl = (*it_sv)->pmap().begin(); it_pl != (*it_sv)->pmap().end(); ++it_pl)
			if (regexec(g_crx_pnm, (*it_pl)->getName(), 0, NULL, 0) == 0)
				dest->push_back(*it_pl);

	/* return the number of matched players */
	return dest->size() - prevsize;
}

/* since we made sure we only print sane raw output, we will mostly
 * rely on that data being valid here. your problem if you feed crap */
bool eat_raw(FILE *str, int *num_failed_gs)
{
	size_t len = 0;
	ssize_t read;
	Server *sv;
	bool first = true, ret = false;
	char *adr = NULL, *map = NULL, *typ = NULL, *ver = NULL, *snm = NULL, *pnm = NULL, *line = NULL, *tmp;
	int failsz, adrlen, npl, mpl, flg, prg, sco, /*read data into these */
	ml_adr, ml_snm, ml_typ, ml_map, ml_ver, ml_pnm; /* maximum string lengths */

	if (!str)
		return false;

	while ((read = getline(&line, &len, str)) != -1) {
		if (strlen(lrtrim(line)) < 2)
			continue;
		if (first) {
			if (sscanf(line, "D;%*u;%*i;%*i;%*i;%i;%*u;%*u;%i;%i;%i;%i;%i;%i\n", &failsz, &ml_adr,
			                &ml_snm, &ml_typ, &ml_map, &ml_ver, &ml_pnm) < 7)
				goto er_bail_out;

			adr = (char*)malloc(ml_adr + 1);
			map = (char*)malloc(ml_map + 1);
			typ = (char*)malloc(ml_typ + 1);
			ver = (char*)malloc(ml_ver + 1);
			snm = (char*)malloc(ml_snm + 1);
			pnm = (char*)malloc(ml_pnm + 1);

			first = false;
			continue;
		}

		if (line[0] == 'S' && line[1] == ';') {
			adrlen = strchr(line + 2, ';') - (line + 2);
			if (adrlen < 9 && adrlen > ml_adr)
				goto er_bail_out;

			strncpy(adr, line + 2, adrlen);
			adr[adrlen] = '\0';

			if (sscanf(line + 2 + adrlen, ";%i;%i;%x;%i\n", &npl, &mpl, &flg, &prg) < 4)
				goto er_bail_out;

			if ((read = getline(&line, &len, str)) == -1) goto er_bail_out;
			strncpy(typ, lrtrim(line), ml_typ);

			if ((read = getline(&line, &len, str)) == -1) goto er_bail_out;
			strncpy(map, lrtrim(line), ml_map);

			if ((read = getline(&line, &len, str)) == -1) goto er_bail_out;
			strncpy(ver, lrtrim(line), ml_ver);

			if ((read = getline(&line, &len, str)) == -1) goto er_bail_out;
			strncpy(snm, lrtrim(line), ml_snm);

			/* make sure everything is zero-terminated */
			typ[ml_typ] = map[ml_map] = ver[ml_ver] = snm[ml_snm] = '\0';

			sv = new Server(adr);

			sv->setVersion(ver);
			sv->setName(snm);
			sv->setMap(map);
			sv->setGameType(typ);
			sv->setFlags(flg);
			sv->setProgress(prg);
			sv->setNumPlayers(npl);
			sv->setMaxPlayers(mpl);

			g_list_done.push_back(sv);

		} else if (line[0] == 'P' && line[1] == ';') {
			if (sscanf(line, "P;%i;", &sco) < 1)
				goto er_bail_out;

			if (!(tmp = strchr(line + 2, ';')))
				goto er_bail_out;
			++tmp;

			strncpy(pnm, tmp, ml_pnm);

			pnm[ml_pnm] = '\0';

			sv->addPlayer(pnm, sco);
		} else {
			DBG(0, "crap line read from raw input: \"%s\", skipping\n",line);
		}

	}
	if (num_failed_gs)
		*num_failed_gs = failsz;
	ret = !g_list_done.empty();
er_bail_out:
	if (!ret) DBG(0, "broken input data, last line was \"%s\"\n", line);
	free(line); free(adr); free(map); free(typ); free(ver); free(snm); free(pnm);
	if (!ret && sv)
		sv->clearPlayers();

	return ret;
}

void output_raw(std::list<Server*> *slist)
{
	Server *sv;
	std::list<Server*>::const_iterator it_sv;
	std::set<Player*>::const_iterator it_pl;

	if (!g_str_raw || slist->empty())
		return;

	for (it_sv = slist->begin(); it_sv != slist->end(); ++it_sv) {
		sv = *it_sv;
		fprintf(g_str_raw, "S;%s;%i;%i;%x;%i\n%s\n%s\n%s\n%s\n", sv->getHost(), sv->getNumPl(),
		                sv->getMaxPl(), sv->getFlags(), sv->getProgress(), sv->getType(), sv->getMap(),
		                sv->getVer(), sv->getName());

		if (!sv->pmap().empty())
			for (it_pl = sv->pmap().begin(); it_pl != sv->pmap().end(); ++it_pl)
				fprintf(g_str_raw, "P;%i;%s\n", (*it_pl)->getScore(), (*it_pl)->getName());
	}
}

void output_servers(std::list<Server*> *slist)
{
	char outbuf[OUT_MAX_LINE_LEN], /* stores a line to be printed out */
	     frm[14]; /* stores a potentially colored g_frame_char, or an empty string when not displ. a frame*/

	Server *sv;
	std::list<Server*>::const_iterator it_sv;
	std::set<Player*>::const_iterator it_pl;

	int sv_line_len; /* used for formatting, to keep track of how many chars were printed */
	int fw_adr,      /* output field width for the server addr:port string */
	    fw_typ,      /* output field width for server gametype */
	    fw_map,      /* output field width for server map */
	    fw_snm,      /* output field width for server name */
	    /*when also displaying players: */
	    fw_pnm,      /* output field width for player name */
	    pl_line_len, /*  like sv_line_len but for player lines */
	    pl_column,   /* keeps track in which column we are when displ. players with more than 1 per line */
	    pl_per_line; /* determines how many players to fit on a line */

	size_t maxpname, /* used for finding the longest player name, if we align on a per-server basis */
	       c = 0;    /* counts iterations */

	/* crap call */
	if (!g_str_out || slist->empty())
		return;

	/* set field widths for server fields, by clamping the longest strings against the
	 * argument-provided hard limits on field widths */
	fw_adr = clamp(g_maxlen_s_adr, g_fw_min_s_adr, g_fw_max_s_adr);
	fw_typ = clamp(g_maxlen_s_typ, g_fw_min_s_typ, g_fw_max_s_typ);
	fw_map = clamp(g_maxlen_s_map, g_fw_min_s_map, g_fw_max_s_map);
	fw_snm = clamp(g_maxlen_s_snm, g_fw_min_s_snm, g_fw_max_s_snm);

	/* prepare frame char if we're going to display a frame */
	if (g_display_frame)
		sprintf(frm, "%s%c%s", get_color(CO_FRM), g_frame_char, get_color(0, 0));
	else *frm = '\0';

	for (it_sv = slist->begin(); it_sv != slist->end(); ++it_sv) {
		++c;
		sv = *it_sv;

		/*TODO: split this into several parts */
		/* get_color() will return empty strings if in non-colored mode,
		 * frm will be empty when we're not going to draw a frame */
		sv_line_len = snprintf(outbuf, sizeof outbuf,
		    "%s%s%s%-*.*s%s %s %s%-*.*s%s %s %s%-*.*s%s %s %s%2i/%2i%s %s %s%-*.*s%s%s%s%s",
		        frm, g_display_frame?" ":"",
		        get_color(CO_ADR), fw_adr, fw_adr, sv->getHost(),  get_color(0, 0), frm,
		        get_color(CO_TYP), fw_typ, fw_typ, sv->getType(),  get_color(0, 0), frm,
		        get_color(CO_MAP), fw_map, fw_map, sv->getMap(),   get_color(0, 0), frm,
		        get_color(CO_NPL), sv->getNumPl(), sv->getMaxPl(), get_color(0, 0), frm,
		        get_color(CO_SNM), fw_snm, fw_snm, sv->getName(),  get_color(0, 0),
		        g_display_frame?" ":"", frm, (g_skip_last_nl && c == slist->size())?"":"\n");

		/* color sequences don't cause any output, so we need to subtract the color overhead again.
		 * we have at least five color sequences (and their reset sequences) which are always there, plus
		 * six additional ones when displaying a frame */
		if (g_colored)
			sv_line_len -= (5 + (g_display_frame?6:0)) * COLOR_OVERHEAD;
		if (g_skip_last_nl && c == slist->size())
			--sv_line_len; /* subtract the trailing newline as well, if there is one */

		/* if this is the first iteration, and we want a frame, display the head line */
		if (g_display_frame && it_sv == slist->begin())
			fputs(get_frameline(sv_line_len), g_str_out);

		/* now display our actual line of output sprintf'ed into outbuf before */
		fputs(outbuf, g_str_out);

		if (g_sv_show_players) {
			pl_column = 0;

			/* if we're not going to globally align players in colums, we need to know the
			 * longest name among the players on this particular server. */
			if (!g_global_palign) {
				maxpname = 0;
				for (it_pl = sv->pmap().begin(); it_pl != sv->pmap().end(); ++it_pl)
					if (strlen((*it_pl)->getName()) > maxpname)
						maxpname = strlen((*it_pl)->getName());
			}

			/* calculate the final player field widths by clamping the global or local longest names
			 * against the argument-provided hard limits on player name field width */
			fw_pnm = clamp(g_global_palign?g_maxlen_s_pnm:maxpname, g_fw_min_s_pnm, g_fw_max_s_pnm);

			pl_per_line = g_players_per_line;

			/* if pl_per_line is 0, we're going to auto detect how many players to fit on one line */
			if (pl_per_line == 0)
				pl_per_line = (sv_line_len - 3) / (fw_pnm + 6);

			pl_line_len = 0;
			/* walk over the players on the current server */
			for (it_pl = sv->pmap().begin(); it_pl != sv->pmap().end(); ++it_pl) {
				pl_line_len +=
				    fprintf(g_str_out, "%s%s%s\\___%s %s%-*.*s%s ",
				        (pl_column == 0)?frm:"", (pl_column == 0)?(g_display_frame?" ":""):"",
				        get_color(CO_ARR), get_color(0, 0),
				        get_color(CO_POS), fw_pnm, fw_pnm, (*it_pl)->getName(), get_color(0, 0));

				/* subtract color overhead like above */
				if (g_colored)
					pl_line_len -= ((!pl_column && g_display_frame)?3:2) * COLOR_OVERHEAD;

				/* if printed enough players on this line, fill up, maybe print
				 * a frame char and start a new line */
				if ((++pl_column) >= pl_per_line) {
					fprintf(g_str_out, "%*s%s\n", sv_line_len - pl_line_len - 1, "", frm);
					pl_column = pl_line_len = 0;
				}
			}
			if (pl_column > 0)
				fprintf(g_str_out, "%*s%s\n", sv_line_len - pl_line_len - 1, "", frm);
		}
		/* display a separating frame line, if wished */
		if (g_display_frame && g_display_seps)
			fputs(get_frameline(0), g_str_out);
	}
	/* display a trailing frame line, if wished and not yet done */
	if (g_display_frame && !g_display_seps)
		fputs(get_frameline(0), g_str_out);
}

void output_players(std::list<Player*> *plist)
{
	char outbuf[OUT_MAX_LINE_LEN]; /* stores a line to be printed out */
	char frm[14]; /* stores a potentially colored g_frame_char, or an empty string when not displ. a frame*/

	Server *sv;
	Player *pl;
	std::list<Player*>::const_iterator it_pl;

	int fw_adr,      /* output field width for the server addr:port string */
	    fw_typ,      /* output field width for server gametype */
	    fw_map,      /* output field width for server map */
	    fw_snm,      /* output field width for server name */
	    fw_pnm,      /* output field width for player name */
	    line_len;    /* used for formatting, to keep track of how many chars were printed */

	size_t c = 0;    /* counts iterations */

	/* crap call */
	if (!g_str_out || plist->empty())
		return;

	/* prepare frame char if we're going to display a frame */
	if (g_display_frame)
		sprintf(frm, "%s%c%s", get_color(CO_FRM), g_frame_char, get_color(0, 0));
	else
		frm[0] = '\0';

	/* set field widths for server fields, by clamping the longest strings against the
	 * argument-provided hard limits on field widths */
	fw_adr = clamp(g_maxlen_p_adr, g_fw_min_p_adr, g_fw_max_p_adr);
	fw_typ = clamp(g_maxlen_p_typ, g_fw_min_p_typ, g_fw_max_p_typ);
	fw_map = clamp(g_maxlen_p_map, g_fw_min_p_map, g_fw_max_p_map);
	fw_snm = clamp(g_maxlen_p_snm, g_fw_min_p_snm, g_fw_max_p_snm);
	fw_pnm = clamp(g_maxlen_p_pnm, g_fw_min_p_pnm, g_fw_max_p_pnm);

	/* walk over player list supplied to be printed out */
	for (it_pl = plist->begin(); it_pl != plist->end(); ++it_pl) {
		++c;
		pl = *it_pl;
		sv = pl->getServer();

		/*TODO: split this into several parts */
		/* get_color() will return empty strings if in non-colored mode,
		 * frm will be empty when we're not going to draw a frame */
		line_len = snprintf(outbuf, sizeof outbuf,
		    "%s%s%s%-*.*s%s %s %s%-*.*s%s %s %s%-*.*s%s %s %s%-*.*s%s %s %s%2i/%2i%s %s %s%-*.*s%s%s%s%s",
		        frm, g_display_frame?" ":"",
		        get_color(CO_PNM), fw_pnm, fw_pnm, pl->getName(),  get_color(0, 0), frm,
		        get_color(CO_ADR), fw_adr,  fw_adr,  sv->getHost(),  get_color(0, 0), frm,
		        get_color(CO_TYP), fw_typ, fw_typ, sv->getType(),  get_color(0, 0), frm,
		        get_color(CO_MAP), fw_map,  fw_map,  sv->getMap(),   get_color(0, 0), frm,
		        get_color(CO_NPL),       sv->getNumPl(), sv->getMaxPl(), get_color(0, 0), frm,
		        get_color(CO_SNM), fw_snm, fw_snm, sv->getName(),  get_color(0, 0),
		        g_display_frame?" ":"", frm, (g_skip_last_nl && c == plist->size())?"":"\n");

		/* if this is the first iteration, and we want a frame, display the head line */
		if (g_display_frame && it_pl == plist->begin())
			fputs(get_frameline(line_len - 1 - (!g_colored ? 0
					: ((6 + (g_display_frame?7:0)) * COLOR_OVERHEAD))), g_str_out);

		/* now display our actual line of output sprintf'ed into outbuf before */
		fputs(outbuf, g_str_out);

		/* display a separating frame line, if wished */
		if (g_display_frame && g_display_seps)
			fputs(get_frameline(0), g_str_out);
	}
	/* display a trailing frame line, if wished and not yet done */
	if (g_display_frame && !g_display_seps)
		fputs(get_frameline(0), g_str_out);
}

void gather_s_format_hints(std::list<Server*> *slist, int *ml_adr, int *ml_nam, int *ml_typ, int *ml_map,
		int *ml_ver, int *ml_pnm)
{
	Server *sv;
	int len;
	std::list<Server*>::const_iterator it_sv;
	std::set<Player*>::const_iterator it_pl;

	if (ml_adr) *ml_adr = 0;
	if (ml_nam) *ml_nam = 0;
	if (ml_typ) *ml_typ = 0;
	if (ml_map) *ml_map = 0;
	if (ml_ver) *ml_ver = 0;
	if (ml_pnm) *ml_pnm = 0;

	for (it_sv = slist->begin(); it_sv != slist->end(); ++it_sv) {
		sv = *it_sv;

		if (ml_adr && (len = strlen(sv->getHost())) > *ml_adr) *ml_adr = len;
		if (ml_nam && (len = strlen(sv->getName())) > *ml_nam) *ml_nam = len;
		if (ml_typ && (len = strlen(sv->getType())) > *ml_typ) *ml_typ = len;
		if (ml_map && (len = strlen(sv->getMap()))  > *ml_map) *ml_map = len;
		if (ml_ver && (len = strlen(sv->getVer()))  > *ml_ver) *ml_ver = len;

		if (ml_pnm)
			for (it_pl = sv->pmap().begin(); it_pl != sv->pmap().end(); ++it_pl)
				if ((len = strlen((*it_pl)->getName())) > *ml_pnm)
					*ml_pnm = len;

	}
}

void gather_p_format_hints(std::list<Player*> *plist, int *ml_adr, int *ml_snm, int *ml_typ, int *ml_map,
		int *ml_ver, int *ml_pnm)
{
	int max_pname = 0, tmp;
	std::list<Server*> slist;
	std::list<Player*>::const_iterator it_pl;

	for (it_pl = plist->begin(); it_pl != plist->end(); ++it_pl) {
		slist.push_back((*it_pl)->getServer());
		if ((tmp = strlen((*it_pl)->getName())) > max_pname)
			max_pname = tmp;
	}

	gather_s_format_hints(&slist, ml_adr, ml_snm, ml_typ, ml_map, ml_ver, NULL);

	*ml_pnm = max_pname;
}

bool sane_player(const char *pnm,const char *sco)
{
	return (pnm && sco && *pnm && *sco && is_numeric(sco));
}

bool sane_server(const char *ver,const char *snm,const char *map,const char *typ,
		const char *flg,const char *prg,const char *npl,const char *mpl)
{
	return  ver &&  snm &&  map &&  typ &&  prg &&  flg &&  npl &&  mpl
	    && *ver && *snm && *map && *typ && *prg && *flg && *npl && *mpl
	    && is_numeric(prg) && is_numeric(flg) && is_numeric(npl) && is_numeric(mpl);
}

char *lrtrim(char *s)
{
	char *sptr, *tmp;

	if (!s || strlen(s) == 0)
		return s;

	sptr = s + strlen(s) - 1;
	while (sptr >= s && (*sptr <= 0x20 || *sptr == 0x7f))
		*(sptr--) = 0;

	sptr = s;
	while (*sptr && (*sptr <= 0x20 || *sptr == 0x7f))
		++sptr;

	tmp = strdup(sptr);
	strcpy(s, tmp);
	free(tmp);
	return s;
}

char *sanitize(char *s, bool strict)
{
	unsigned char c;
	char *tmp;

	if (!s || strlen(s) == 0)
		return s;

	tmp = s;
	for (; (c = (unsigned char)*tmp); ++tmp)
		if (c < 0x20u || c == 0x7fu)
			*tmp = g_sane_char;
		else if (strict && c > 0x7fu)
			*tmp = g_sane_char_strict;

	return lrtrim(s);
}

bool is_numeric(const char *s)
{
	bool gotdig = false;
	if (!s || !(*s))
		return false;

	if (*s == '-' || *s == '+')
		++s;

	for (char c; (c = *s); ++s)
		if (!isdigit(c))
			return false;
		else
			gotdig = true;

	return gotdig;
}

const char *get_color(int attr, int col)
{
	char wbuf[18];
	int ckey;

	if (!g_colored)
		return "";

	if (!g_color_map.count(ckey = ((attr & 0xff) << 8) | (col & 0xff))) {
		if (ckey == 0) {
			sprintf(wbuf, "\033[0m");
		} else {
			sprintf(wbuf, "\033[%.2i;%.2im", attr, col);
		}
		g_color_map[ckey] = strdup(wbuf);
	}
	return g_color_map[ckey];
}

const char *get_frameline(int linelen)
{
	static char *last = NULL;
	int reallen;
	char *tmp;

	if (!linelen)
		return last;

	free(last);
	reallen = linelen + (g_colored?COLOR_OVERHEAD:0) + 1;
	last = (char*) malloc(reallen + 1);
	last[0] = '\0';
	if (g_colored)
		strcpy(last, get_color(CO_FRM));

	tmp = last + strlen(last);
	memset(tmp, g_frame_char, linelen);
	tmp[linelen] = '\0';
	if (g_colored)
		strcat(last, get_color(0, 0));
	last[reallen - 1] = '\n';
	last[reallen] = '\0';
	return last;
}

u_int16_t getWord(const void* data)
{
	return ntohs(*((u_int16_t*)data));
}

u_int16_t getWordNC(const void* data)
{
	return *((u_int16_t*)data);
}

u_int32_t getDWord(const void* data)
{
	return ntohl(*((u_int32_t*)data));
}

u_int32_t getDWordNC(const void* data)
{
	return *((u_int32_t*)data);
}

u_int64_t getMSTimestamp(const struct timeval *tv)
{
	return ((u_int64_t)(tv->tv_sec)) * 1000 + tv->tv_usec / 1000;
}

u_int64_t getMSInterval(struct timeval *from, struct timeval *to)
{
	return getMSTimestamp(to) - getMSTimestamp(from);
}

int clamp(int v, int min, int max)
{
	if (v < min)
		v = min;
	else if (v > max)
		v = max;
	return v;
}

int make_paket(unsigned char *buf, const unsigned char *pkt, size_t len)
{
	int z;

	for (z = 0; z < 6; ++z)
		buf[z] = 0xff;

	memcpy(buf + 6, pkt, len);

	return 6 + len;
}

int mkpk_getlist(unsigned char *buf)
{
	return make_paket(buf, P_GETLIST, sizeof P_GETLIST);
}

int mkpk_getinfo(unsigned char *buf)
{
	return make_paket(buf, P_GETINFO, sizeof P_GETINFO);
}

int mkpk_getcount(unsigned char *buf)
{
	return make_paket(buf, P_GETCOUNT, sizeof P_GETCOUNT);
}

/*takes const char*s and needs a null pointer as the very last argument*/
void dump_strings(FILE *str, ...)
{
	char buf[256], *sel_buf, *xl_buf = NULL;
	size_t bufsz, arglen;
	const char *arg;
	va_list vl;

	va_start(vl,str);

	while ((arg = va_arg(vl,const char*))) {
		arglen = 4 * strlen(arg) + 1;

		if (arglen > sizeof buf) {
			sel_buf = xl_buf = (char*)malloc(bufsz = arglen);
		} else {
			sel_buf = buf;
			bufsz = sizeof buf;
		}
		dump_string(sel_buf, arg, bufsz);
		fprintf(str,"\"%s\"\n",sel_buf);
		free(xl_buf);
		xl_buf = NULL;
	}

	va_end(vl);
}

size_t hexdump(char *dest, const char *s, size_t bufsz)
{
	size_t slen, chmax, z;
	char tmpbuf[4];
	const unsigned char *ptr;

	if (!dest || !s || (bufsz > 0 && (*dest = 0), bufsz < 3) || !(*s))
		return 0;

	while (bufsz % 3)
		--bufsz;

	chmax = ((slen = strlen(s)) * 3 > bufsz) ? bufsz / 3 : slen;

	ptr = (const unsigned char*)s;
	for (z = 0; z < chmax; ++z, ++ptr) {
		sprintf(tmpbuf, "%02x%c", *ptr, (z + 1 < chmax) ? ' ' : '\0');
		strcat(dest, tmpbuf);
	}
	dest[chmax * 3 - 1] = '\0';
	return chmax * 3 - 1;
}

size_t dump_string(char *dest, const char *s, size_t bufsz)
{
	size_t hdlen, remain, slen;
	char *san;

	if (!(hdlen = hexdump(dest, s, bufsz))) {
		return 0;
	}
	remain = bufsz - (hdlen + 1); // this expr will always evaluate to sth >= 0 (not only because it's unsigned)
	if (remain < 2)
		return hdlen;
	strcat(dest, " ");
	--remain;
	san = sanitize(strdup(s), true);
	slen = strlen(san);
	strncat(dest, san, remain);
	remain -= (slen >= remain) ? remain : slen;
	return bufsz - remain - 1;
}

/*warning: biohazard below. TODO switch to getopt & friends*/
bool process_args(int argc, char **argv)
{
	char *tmp, *tok;
	int z;
	FILE *str_nerr;
	for (z = 1; z < argc; ++z) {
		if (strcmp("-v", argv[z]) == 0) {
			++g_verb;
		} else if (strcmp("-P", argv[z]) == 0) {
			if (z + 1 < argc && argv[z + 1][0] && argv[z + 1][0] != '-')
				g_progress_bar = strtol(argv[++z], NULL, 10);
			else
				g_progress_bar = 80;
		} else if (strcmp("-E", argv[z]) == 0) {
			if (z + 1 < argc) {
				tmp = strdup(argv[++z]);
				if (!(str_nerr = fopen(tmp, g_error_append?"a":"w"))) {
					perror(tmp);
					DBG(0, "error channel is still stderr\n");
				} else {
					if (g_str_err) fclose(g_str_err);
					g_str_err = str_nerr;
				}
			}
		} else if (strcmp("-nn", argv[z]) == 0) {
			g_skip_last_nl = true;
		} else if (strcmp("-h", argv[z]) == 0) {
			usage(stdout, argv[0], EXIT_SUCCESS);
		} else if (strcmp("-m", argv[z]) == 0) {
			if (z + 1 < argc) {
				if (!(tok = strtok(tmp = strdup(argv[++z]), ",")))
					{free(tmp);return false;}
				do {g_list_masters.push_back(strdup(tok));} while ((tok = strtok(NULL, ",")));
				free(tmp);
			} else
				return false;
		} else if (strcmp("-o", argv[z]) == 0) {
			if (z + 1 < argc) g_output_file = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-I", argv[z]) == 0) {
			if (z + 1 < argc) g_input_file = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-f", argv[z]) == 0) {
			if (z + 1 < argc) g_svl_tol = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-F", argv[z]) == 0) {
			g_display_frame = true;
		} else if (strcmp("-FS", argv[z]) == 0) {
			g_display_seps = true;
		} else if (strcmp("-l", argv[z]) == 0) {
			if (z + 1 < argc) g_players_per_line = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-L", argv[z]) == 0) {
			g_global_palign = true;
		} else if (strcmp("-ss", argv[z]) == 0) {
			if (z + 1 < argc && argv[z + 1][0] && argv[z + 1][0] != '-')
				g_sane_char_strict = argv[++z][0];
			else
				g_strict_sanitize = false;
		} else if (strcmp("-S", argv[z]) == 0) {
			g_summary = true;
		} else if (strcmp("-ao", argv[z]) == 0) {
			g_output_append = true;
		} else if (strcmp("-aE", argv[z]) == 0) {
			g_error_append = true;
		} else if (strcmp("-aO", argv[z]) == 0) {
			g_raw_output_append = true;
		} else if (strcmp("-sc", argv[z]) == 0) {
			if (z + 1 < argc && argv[z + 1][0] && argv[z + 1][0] != '-')
				g_sane_char = argv[++z][0];
			else return false;
		} else if (strcmp("-O", argv[z]) == 0) {
			if (z + 1 < argc) g_raw_file = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-c", argv[z]) == 0) {
			g_colored = true;
		} else if (strcmp("-i", argv[z]) == 0) {
			g_case_insensitive = true;
		} else if (strcmp("-fc", argv[z]) == 0) {
			if (z + 1 < argc) g_frame_char = *argv[++z];
			else return false;
		} else if (strcmp("-r", argv[z]) == 0) {
			if (z + 1 < argc) g_ms_retry = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-R", argv[z]) == 0) {
			if (z + 1 < argc) g_gs_retry = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-T", argv[z]) == 0) {
			if (z + 1 < argc) g_num_threads = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-N", argv[z]) == 0) {
			if (z + 1 < argc) g_lead_nl = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-tm", argv[z]) == 0) {
			if (z + 1 < argc) g_recv_timeout_ms = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-tg", argv[z]) == 0) {
			if (z + 1 < argc) g_recv_timeout_gs = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-p", argv[z]) == 0) {
			if (z + 1 < argc) g_rexp_p_pnm = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-st", argv[z]) == 0) {
			if (z + 1 < argc) g_rexp_s_typ = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-sn", argv[z]) == 0) {
			if (z + 1 < argc) g_rexp_s_snm = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-sa", argv[z]) == 0) {
			if (z + 1 < argc) g_rexp_s_adr = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-sm", argv[z]) == 0) {
			if (z + 1 < argc) g_rexp_s_map = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-sv", argv[z]) == 0) {
			if (z + 1 < argc) g_rexp_s_ver = strdup(argv[++z]);
			else return false;
		} else if (strcmp("-se", argv[z]) == 0) {
			g_no_empty_sv = true;
		} else if (strcmp("-n", argv[z]) == 0) {
			g_no_match_info = true;
		} else if (strcmp("-sp", argv[z]) == 0) {
			g_sv_show_players = true;

		/* ---- output field limits ---- */
		} else if (strcmp("-wsa", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_min_s_adr = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-wst", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_min_s_typ = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-wsn", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_min_s_snm = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-wsm", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_min_s_map = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-wsp", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_min_s_pnm = strtol(argv[++z], NULL, 10);
			else return false;

		} else if (strcmp("-Wsa", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_max_s_adr = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-Wst", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_max_s_typ = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-Wsn", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_max_s_snm = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-Wsm", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_max_s_map = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-Wsp", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_max_s_pnm = strtol(argv[++z], NULL, 10);
			else return false;

		} else if (strcmp("-wpa", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_min_p_adr = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-wpt", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_min_p_typ = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-wpn", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_min_p_snm = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-wpm", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_min_p_map = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-wpp", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_min_p_pnm = strtol(argv[++z], NULL, 10);
			else return false;

		} else if (strcmp("-Wpa", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_max_p_adr = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-Wpt", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_max_p_typ = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-Wpn", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_max_p_snm = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-Wpm", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_max_p_map = strtol(argv[++z], NULL, 10);
			else return false;
		} else if (strcmp("-Wpp", argv[z]) == 0) {
			if (z + 1 < argc) g_fw_max_p_pnm = strtol(argv[++z], NULL, 10);
			else return false;
		/* stop processing arguments on "--" */
		} else if (strcmp("--", argv[z]) == 0) {
			return true;
		} else {
			/*yes we can*/
			DBG(0, "argument mess: wtf is \"%s\" supposed to mean?!\n", argv[z]);
			return false;
		}
	}//TODO warn about missing parameter for last arg
	return true;
}

void usage(FILE *str,const char *a0, int ec)
{
	if (str) {
		/*this fits on 80x25, complete list is in README*/
		fprintf(str, "usage: %s [parameters]\n", a0);
		fprintf(str,
		    "\t-v: increase verbosity (specify twice to further increase)\n"
		    "\t-c: enable bash color sequences\n"
		    "\t-i: case-insensitive matching\n"
		    "\t-se: do not display empty servers\n"
		    "\t-sp: also output players, for matching servers\n"
		    "\t-l NUMBER: in combination with -sp, put NUMBER players on one line\n"
		    "\t-o FILE: write output to file instead of stdout\n"
		    "\t-m STRING: specify a comma seperated list of master servers\n"
		    "\t-r NUMBER: number of retries for unresponsive master servers\n"
		    "\t-R NUMBER: number of retries for unresponsive game servers\n"
		    "\t-T NUMBER: number of threads (default: 20)\n"
		    "\t-tm NUMBER: read timeout for master servers in seconds\n"
		    "\t-tg NUMBER: read timeout for game servers in seconds\n"
		    "\t-p REGEXP: output all players with name matching REGEXP\n"
		    "\t-st REGEXP: output all servers with type matching REGEXP\n"
		    "\t-sn REGEXP: output all servers with name matching REGEXP\n"
		    "\t-sa REGEXP: output all servers with host:port matching REGEXP\n"
		    "\t-sm REGEXP: output all servers with map matching REGEXP\n"
		    "\t-sv REGEXP: output all servers with version matching REGEXP\n"
		    "\t-h: display this usage information statement\n"
		    "!! Consult the README file for a complete list and details !!\n"
		);
	}
	exit(ec);
}

bool init(int argc, char **argv)
{
	int regflags;

	/* has to happen first */
	pthread_mutex_init(&g_mutex_err, NULL);

	if (!process_args(argc, argv))
		return false;

	if (g_skip_last_nl && (g_display_frame || g_sv_show_players)) {
		DBG(0, "warning: -nn cannot be used together with -sp and/or -F, will ignore -nn\n");
		g_skip_last_nl = false;
	}

	if (strcmp("-", g_output_file) != 0)
		g_str_out = NULL; //we fopen later

	if (g_raw_file && strcmp("-", g_raw_file) == 0)
		g_str_raw = stdout;

	if (g_input_file && strcmp("-", g_input_file) == 0)
		g_str_in = stdin;

	pthread_mutex_init(&g_mutex_work, NULL);

	/* set default master servers if none provided through argv */
	if (g_list_masters.empty()) {
		g_list_masters.push_back("master1.teeworlds.com");
		g_list_masters.push_back("master2.teeworlds.com");
		g_list_masters.push_back("master3.teeworlds.com");
		g_list_masters.push_back("master4.teeworlds.com");
	}

	/* compile provided regular expressions */
	regflags = REG_EXTENDED | (g_case_insensitive?REG_ICASE:0);
	if (g_rexp_s_typ && regcomp(g_crx_typ = new regex_t, g_rexp_s_typ, regflags) != 0)
		{delete g_crx_typ;g_crx_typ = NULL;
		DBG(0, "could not compile gametype regexp, ignoring\n");}
	if (g_rexp_s_snm && regcomp(g_crx_snm = new regex_t, g_rexp_s_snm, regflags) != 0)
		{delete g_crx_snm;g_crx_snm = NULL;
		DBG(0, "could not compile svname regexp, ignoring\n");}
	if (g_rexp_s_adr && regcomp(g_crx_adr = new regex_t, g_rexp_s_adr, regflags) != 0)
		{delete g_crx_adr;g_crx_adr = NULL;
		DBG(0, "could not compile adr regexp, ignoring\n");}
	if (g_rexp_s_map && regcomp(g_crx_map = new regex_t, g_rexp_s_map, regflags) != 0)
		{delete g_crx_map;g_crx_map = NULL;
		DBG(0, "could not compile map regexp, ignoring\n");}
	if (g_rexp_s_ver && regcomp(g_crx_ver = new regex_t, g_rexp_s_ver, regflags) != 0)
		{delete g_crx_ver;g_crx_ver = NULL;
		DBG(0, "could not compile version regexp, ignoring\n");}
	if (g_rexp_p_pnm && regcomp(g_crx_pnm = new regex_t, g_rexp_p_pnm, regflags) != 0)
		{delete g_crx_pnm;g_crx_pnm = NULL;
		DBG(0, "could not compile plname regexp, will not match any players\n");}

	return true;
}

void purge()
{
	std::map<int, char*>::const_iterator it_col;
	std::list<Server*>::const_iterator it_sv;

	for (it_col = g_color_map.begin(); it_col != g_color_map.end(); ++it_col)
		free(it_col->second);

	g_color_map.clear();

	for (it_sv = g_list_done.begin(); it_sv != g_list_done.end(); ++it_sv)
		delete *it_sv;
	for (it_sv = g_list_fail.begin(); it_sv != g_list_fail.end(); ++it_sv)
		delete *it_sv;
}

/* thread function */
void *process_queue(void*arg)
{
	Server *sv;
	bool ok;
	int tid, z;
	char *pbbuf = NULL;	
	int *args = (int*)arg;
	int numsv;
	
	tid = args[0];
	numsv = args[1];
	if (numsv == 0) numsv = 1; //we'll divide
	if (g_progress_bar) pbbuf = (char*)malloc(g_progress_bar+1);
	
	for (;;) {
		sv = NULL;

		pthread_mutex_lock(&g_mutex_work);
		if (!g_list_work.empty()) {
			sv = g_list_work.front();
			g_list_work.pop_front();
			pthread_mutex_unlock(&g_mutex_work);

			pthread_mutex_lock(&g_mutex_err);
			
			if (g_progress_bar) {
				fputs("\r|", g_str_err);
				z = (int)(((double)g_progress_bar * (numsv - g_list_work.size())) / numsv);
				while(z--) {
					fputc('=', g_str_err);
				}
				fputs(">", g_str_err);
				g_err_dirty = true;
			} else {
				DBG(2, "\rthread %02i: %i servers remaining      ", tid, g_list_work.size());
				if (g_verb >= 2) g_err_dirty = true;

			}
			pthread_mutex_unlock(&g_mutex_err);
		} else {
			pthread_mutex_unlock(&g_mutex_work);
			if (!g_progress_bar) {
				pthread_mutex_lock(&g_mutex_err);
				DBG(2, "\rthread %02i done                       ", tid);
				if (g_verb >= 2) g_err_dirty = true;
				pthread_mutex_unlock(&g_mutex_err);
			}
			break;
		}

		ok = acquire_details(sv, g_gs_retry);

		pthread_mutex_lock(&g_mutex_work);
		(ok?g_list_done:g_list_fail).push_back(sv);
		pthread_mutex_unlock(&g_mutex_work);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t *threads; /* array of thread identifiers we're going to use */

	int **tids,        /* thread id and server count (for progress bar display) */
	    z,             /* thread spawn/join counter */
	    pcount,        /* player count, only calculated when we're interested in */
	    sucm = 0,      /* number of successfully contacted master servers */
	    numdup = 0,    /* number of game servers which have been announced from several master servers */
	    sv_matchc,     /* number of matched servers */
	    pl_matchc,     /* number of matched players */
	    num_failed_gs; /* number of unresponsive or crappy game servers */

	timeval tmstart, tmend, /* start and end time of master server communication */
	        tgstart, tgend; /* start and end time of game server communication */

	std::set<u_int64_t> dup_kill; /* used for eliminating duplicate game servers */
	std::list<u_int64_t> tmplist; /* tmp list holding server addr until server objects are instantiated */

	std::list<u_int64_t>::const_iterator it_addr; /* used for walking over addresses */
	std::list<Server*>::const_iterator it_sv;     /* used for walking over server lists */

	std::list<Server*> list_svmatch; /* will contain all matched servers */
	std::list<Player*> list_plmatch; /* will contain all matched players */

	std::set<int>      tfail; /* will remember which threads we could not create */

	/* ========================= initialization ========================= */


	/* if initialization fails, print usage and say goodbye */
	if (!init(argc, argv))
		usage(g_str_err, argv[0], EXIT_FAILURE);


	/* do we want to eat a raw log? we wont proceed if we cannot open it */
	if (!g_str_in && g_input_file && !(g_str_in = fopen(g_input_file, "r"))) {
		perror(g_input_file);
		exit(EXIT_FAILURE);
	}


	/* we only acquire server lists and request details from game
	 * servers when not reading data from a raw log */
	if (!g_str_in) {

		/* ======================= master server part ======================= */

		/* store time, get all master server stuff done, store time again */
		gettimeofday(&tmstart, NULL);
		acquire_all_lists(&tmplist, &sucm);
		gettimeofday(&tmend, NULL);

		if (sucm == 0 || tmplist.empty()) {
			DBG(0,"failed to retrieve anything from any master server (tried %llu ms), giving up\n",
					getMSInterval(&tmstart,&tmend));
			exit(EXIT_FAILURE);
		}

		/* instantiate server objects while eliminating duplicates */
		for (it_addr = tmplist.begin(); it_addr != tmplist.end(); ++it_addr) {

			/* already seen this address? */
			if (dup_kill.count(*it_addr)) {
				++numdup;
				continue;
			}

			/* remember this address */
			dup_kill.insert(*it_addr);

			/* create server object for this address and store in our server queue */
			g_list_work.push_back(new Server(*it_addr));
		}


		/* ======================== game server part ======================== */


		/* allocate thread stuff, init thread ids */
		threads = (pthread_t*)malloc((sizeof (pthread_t)) * g_num_threads);
		tids = (int**)malloc((sizeof(int*)) * g_num_threads);
		for (z = 0; z < g_num_threads; ++z) {
			tids[z] = (int*)malloc(sizeof(int)*2);
			tids[z][0] = z;
			tids[z][1] = g_list_work.size();
		}

		DBG(1, "got %i servers from master servers (%i duplicates, took %llu ms), now fetching details\n",
				g_list_work.size(), numdup, getMSInterval(&tmstart,&tmend));

		/* store time, spawn all threads, wait for them to terminate, store time */
		gettimeofday(&tgstart, NULL);
		for (z = 0; z < g_num_threads; ++z)
			if (pthread_create(&threads[z], NULL, process_queue, tids[z]) != 0) {
				DBG(0, "failed to spawn thread %i\n", z);
				tfail.insert(z);
			}
		for (z = 0; z < g_num_threads; ++z) {
			if (tfail.count(z)) continue;
			if (pthread_join(threads[z], NULL) != 0)
				DBG(0, "failed to join thread %i\n", z);
		}
		gettimeofday(&tgend, NULL);

		/* we dont need this anymore */
		tfail.clear();
		free(threads); 

		for (z = 0; z < g_num_threads; ++z) free(tids[z]);	
		free(tids);

		if (g_err_dirty) DBG(2, "\n");

		if ((num_failed_gs = g_list_fail.size()) > 0) DBG(1, "%i servers failed to respond\n", g_list_fail.size());

	} else { /* we want to read data from raw log instead */
		if (!eat_raw(g_str_in, &num_failed_gs)) {
			DBG(0, "cannot continue like this, exiting\n");
			exit(EXIT_FAILURE); /* fail if we failed to read the input */
		}

	}

	/* ========================== output part =========================== */

	if (!g_str_out && !(g_str_out = fopen(g_output_file, g_output_append?"a":"w"))) {
		perror(g_output_file);
		DBG(0, "output will go to nowhere\n");
	}

	if (g_raw_file && g_str_in) {
		DBG(0, "cannot eat and puke raw output at the same time, disabled raw output\n");
		free(g_raw_file); g_raw_file = NULL;
	}

	if (!g_str_raw && g_raw_file && !(g_str_raw = fopen(g_raw_file, g_raw_output_append?"a":"w"))) {
		perror(g_raw_file);
		DBG(0, "raw output will go to nowhere\n");
	}

	/* calc total player count if we need it */
	pcount = 0;
	if (g_raw_file || g_summary)
		for (it_sv = g_list_done.begin(); it_sv != g_list_done.end(); ++it_sv)
			pcount += (*it_sv)->pmap().size();


	/* match servers and players */
	sv_matchc = match_servers(&list_svmatch, &g_list_done);
	pl_matchc = match_players(&list_plmatch, &g_list_done);

	DBG(1, "%i servers and %i players matched\n",sv_matchc,pl_matchc);

	/* print the headline of raw data */
	if (g_str_raw) {
		/* just for raw output, we will re-gather for our list of matched servers after this */
		gather_s_format_hints(&g_list_done, &g_maxlen_s_adr, &g_maxlen_s_snm, &g_maxlen_s_typ,
				&g_maxlen_s_map, &g_maxlen_s_ver, &g_maxlen_s_pnm);

		fprintf(g_str_raw, "D;%llu;%i;%i;%i;%i;%llu;%llu;%i;%i;%i;%i;%i;%i\n",
				getMSTimestamp(&tmstart), sucm, pcount, g_list_done.size(), num_failed_gs,
				getMSInterval(&tmstart, &tmend), getMSInterval(&tgstart, &tgend),
				g_maxlen_s_adr, g_maxlen_s_snm, g_maxlen_s_typ,
				g_maxlen_s_map, g_maxlen_s_ver, g_maxlen_s_pnm);

		/* output raw data */
		output_raw(&g_list_done);
	}

	/* gather the lengths of the longest strings of each attribute which could be displayed
	 * used for neat output formatting */
	gather_s_format_hints(&list_svmatch, &g_maxlen_s_adr, &g_maxlen_s_snm, &g_maxlen_s_typ, &g_maxlen_s_map,
			&g_maxlen_s_ver, &g_maxlen_s_pnm);

	gather_p_format_hints(&list_plmatch, &g_maxlen_p_adr, &g_maxlen_p_snm, &g_maxlen_p_typ, &g_maxlen_p_map,
			&g_maxlen_p_ver, &g_maxlen_p_pnm);

	if (g_str_out && g_lead_nl && (g_summary || !list_svmatch.empty() || !list_plmatch.empty()))
		while(g_lead_nl--)
			fputs("\n", g_str_out);

	/* output matched servers, if any */
	output_servers(&list_svmatch);

	if (g_skip_last_nl && !list_plmatch.empty()) 
		fputs("\n", g_str_out);

	/* output matched players, if any */
	output_players(&list_plmatch);

	/* output summary line if wished */
	if (g_str_out && g_summary)
		fprintf(g_str_out, "%sglobal: %i players on %i servers (%i missed)%s", g_skip_last_nl?"\n":"", 
				pcount, g_list_done.size(), num_failed_gs, g_skip_last_nl?"":"\n");

	/* close streams */
	if (g_str_out) fclose(g_str_out);
	if (g_str_raw) fclose(g_str_raw);
	if (g_str_err) fclose(g_str_err);
	if (g_str_in)  fclose(g_str_in);

	/* purge for a clean memory analysis dump */
	purge();

	return EXIT_SUCCESS;
}



/* ************************************************************************** */
/* *                          end of implementation                           */
/* ************************************************************************** */
