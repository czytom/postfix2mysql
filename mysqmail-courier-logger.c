#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <mysql/mysql.h>
#include <sys/time.h>
#include <time.h>
#include <limits.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>


#include "mydaemon.h"
#include "myconfig.h"

#define DAEMON_NAME "mysqmail-courier-logger"

int extern errno;
extern char **environ;
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

// Config file reader stuff: using dotconf lib //
mysqmail_config_t mysqmail_config;
// SQL stuffs
MYSQL mysql,*sock;
MYSQL_RES *res;

///////////////////////////////////////////////////////////////////
// A line of log must be parsed and sent to correct mysql record //
///////////////////////////////////////////////////////////////////
#define MAX_LOG_WORDS 32
#define EMAIL_ADDR_SIZE 256
#define MAX_QUERY_SIZE 1024

typedef struct{
	long tv_sec;
	long tv_usec;
}timeval_t;

unsigned int STR_VALUE(char* value){
	if( value == NULL ){
		return 0;
	}else{
		return atoi(value);
	}
}

int log_a_line(char* logline){
/*	Log lines sent by courier should have the following format
Jul 13 15:32:52 new imaplogin: LOGIN, user=username@hostname, ip=[::ffff:xxx.xxx.xxx.xxx], protocol=IMAP
Jul 13 15:39:15 new imaplogin: DISCONNECTED, user=username@hostname, ip=[::ffff:xxx.xxx.xxx.xxx], headers=1590, body=778
Jul 13 15:40:55 new courierpop3login: Connection, ip=[::ffff:127.0.0.1]
Jul 13 15:41:00 new courierpop3login: LOGIN, user=username@hostname, ip=[::ffff:127.0.0.1]
Jul 13 15:41:03 new courierpop3login: LOGOUT, user=username@hostname, ip=[::ffff:127.0.0.1], top=0, retr=0
*/
	char* words[MAX_LOG_WORDS+1];
	char query[MAX_QUERY_SIZE+1]="";
	char* cur_p;
	char* end;
	long mysqmail_bytes = 0;
	char* user;
	char* domain;
	int num_words=0;

	struct timeval tv;
	struct tm *ptr;
	time_t tm;
	char sql_month[60];
	char sql_year[60];
	unsigned int i,j;

#define M_USER 0
#define M_IP 1
#define M_PROTO 2
#define M_HEADER 3
#define M_BODY 4
#define M_TOP 5
#define M_RETR 6
#define M_RCVD 7
#define M_SENT 8
#define M_TIME 9
#define NUM_KEYWORDS 10
	char *keywords[]= {"user", "ip", "protocol", "headers", "body", "top", "retr", "rcvd", "sent", "time"};
	char *values[NUM_KEYWORDS+1];

	// *** Tokenise the line in words ***
	cur_p = strtok(logline," ");
	while(num_words < MAX_LOG_WORDS && (words[num_words++] = cur_p) != NULL){
		cur_p = strtok(NULL," ");
	}
	num_words--;

	if (num_words <= 6){
		return 0; //not enough data on line
	}

	if ( ! (strstr(words[4], "pop3") || strstr(words[4], "imap")) || ! (strstr(words[5], "LOGOUT") || strstr(words[5], "DISCONNECTED"))){
                return 0; //this isn't a courier line logout/disconnected, just exit and log nothing!
	}

	// These ones is very important, because we will use it in our tests later, to know if a keyword is present in the logged line
	values[M_USER] = NULL;
	values[M_IP] = NULL;
	values[M_PROTO] = NULL;
	values[M_HEADER] = NULL;
	values[M_BODY] = NULL;
	values[M_TOP] = NULL;
	values[M_RETR] = NULL;
	values[M_RCVD] = NULL;
	values[M_SENT] = NULL;
	values[M_TIME] = NULL;

	// *** Parse all words in a key / value array ***
	for(i=6;i<num_words;i++){
		// For all tokenised words
		cur_p = words[i];
		for(j=0;j<NUM_KEYWORDS;j++){
			// compare with our keyword list, if matches, store
			if( strstr(cur_p, keywords[j])){
				cur_p += strlen(keywords[j]) + 1;
				end = cur_p + strlen(cur_p) - 1;
				// strip off an eventual comma at end of value
				if( *end == ','){
					*end = '\0';
				}
				values[j] = cur_p;
			}
		}
	}

	// If we have no user, then we have nothing to log...
	if ( values[M_USER] == NULL ){
		syslog(LOG_NOTICE, "No user: %s!\n",values[M_USER]);
		return 0;
	}

	// Calculate a timestamp and a date (year+month)
	gettimeofday(&tv,NULL);
	tm = time(NULL);
	ptr = localtime(&tm);
	strftime(sql_month ,100 , "\%m",ptr);
	strftime(sql_year ,100 , "\%Y",ptr);

	// *** Parse the user value to get the domain part ***
	user = values[M_USER];
	domain = "null";
	if (strstr(user,"@")){
		domain = strstr(user,"@");
		user[strlen(user) - strlen(domain)] = 0; //strip off the domain portion
		domain++; //shift past the @ to get the domain
	}

	// *** Calculate the total traffic for the session ***
	mysqmail_bytes = 0;
	mysqmail_bytes += STR_VALUE(values[M_SENT]);
	mysqmail_bytes += STR_VALUE(values[M_RETR]);
	mysqmail_bytes += STR_VALUE(values[M_RCVD]);

	// Build the queries
	if ( strstr(words[4], "pop3") ){
		sprintf(query,"INSERT IGNORE INTO %s (domain_name,month,year) VALUES ('%s','%s','%s');"
			,mysqmail_config.mysql_table_scoreboard, domain, sql_month, sql_year);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
			return 0;
		}

		sprintf(query,"UPDATE %s SET pop_trafic=pop_trafic+%ld WHERE domain_name='%s' AND month='%s' AND year='%s'"
			,mysqmail_config.mysql_table_scoreboard, mysqmail_bytes, domain, sql_month, sql_year);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
			return 0;
		}
		sprintf(query,"UPDATE %s SET pop3_login_count=pop3_login_count+1 , pop3_transfered_bytes=pop3_transfered_bytes+%ld,last_login=%ld WHERE id='%s' AND mbox_host='%s';", mysqmail_config.mysql_table_pop_access,mysqmail_bytes, (long) tv.tv_sec,user,domain);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
		}
	} else if ( strstr(words[4], "imap") ){
		sprintf(query,"INSERT IGNORE INTO %s (domain_name,month,year) VALUES ('%s','%s','%s');"
			,mysqmail_config.mysql_table_scoreboard, domain, sql_month, sql_year);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
			return 0;
		}
		sprintf(query,"UPDATE %s SET imap_trafic=imap_trafic+%ld \
			WHERE domain_name='%s' AND month='%s' AND year='%s'"
			,mysqmail_config.mysql_table_scoreboard, mysqmail_bytes, domain, sql_month, sql_year);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
			return 0;
		}
		sprintf(query,"UPDATE %s SET imap_login_count=imap_login_count+1 , imap_transfered_bytes=imap_transfered_bytes+%ld,last_login=%ld WHERE id='%s' AND mbox_host='%s';", mysqmail_config.mysql_table_pop_access,mysqmail_bytes, (long) tv.tv_sec,user,domain);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
		}
	}
	return 0;
}

int main(int argc, char **argv){
	int ret;
	daemonize();
	setlogmask (LOG_UPTO (LOG_NOTICE));
	openlog("mysqmail-cour-logger", LOG_NDELAY | LOG_CONS | LOG_PID, LOG_LOCAL1);
	syslog (LOG_NOTICE, "Program started by User %d", getuid ());
	write_pidfile(DAEMON_NAME);
	reg_hand();

	read_config_file();
	do_ze_mysql_connect();
	ret = log_all_lines("syslog",&log_a_line);
	mysql_close(sock);
	return ret;
}
