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

/////////////////////////////////////////////////
// Config file reader stuff: using dotconf lib //
/////////////////////////////////////////////////
mysqmail_config_t mysqmail_config;

// Mysql connect stuff //
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
	long mysqmail_bytes = 0;
	char* user;
	char* domain;
	int num_words=0;
	char* user_str;

	struct timeval tv;
	struct tm *ptr;
	time_t tm;
	char sql_month[60];
	char sql_year[60];
	char* bytes_fld;

	// *** Tokenise the line in words ***
	cur_p = strtok(logline," ");
	while(num_words < MAX_LOG_WORDS && (words[num_words++] = cur_p) != NULL){
		cur_p = strtok(NULL," ");
	}
	num_words--;

	if (num_words <= 6){
		return 0; //not enough data on line
	}

	if ( ! strstr(words[4], "dovecot") || ! (strstr(words[5], "IMAP(") || strstr(words[5], "POP3(")) || ! strstr(words[6], "Disconnected:")){
                return 0; //this isn't a courier line logout/disconnected, just exit and log nothing!
	}


	// Calculate a timestamp and a date (year+month)
//	syslog(LOG_NOTICE, "Found a dov im/po line, calculating timeofday");
	gettimeofday(&tv,NULL);
	tm = time(NULL);
	ptr = localtime(&tm);
	strftime(sql_month ,100 , "\%m",ptr);
	strftime(sql_year ,100 , "\%Y",ptr);

	user_str = malloc( strlen(words[5]) + 1);
	if(user_str == NULL){
		syslog(LOG_NOTICE, "Malloc failed");
		return 0;
	}
	strcpy( user_str, words[5] + strlen("IMAP(") );
	user_str[ strlen(user_str) ] = '\0';

	// *** Parse the user value to get the domain part ***
//	syslog(LOG_NOTICE, "Searching domain");
	user = user_str;
	domain = "null";
	if (strstr(user,"@")){
		domain = strstr(user,"@");
		user[strlen(user) - strlen(domain)] = 0; //strip off the domain portion
		domain++; //shift past the @ to get the domain
	}

	// Strip off the last 2 chars of the domain name which are "):"
	domain[strlen(domain) - 2] = 0;

	// *** Calculate the total traffic for the session ***
	mysqmail_bytes = 0;


	// Build the queries
	// Aug 20 17:31:43 mx dovecot: POP3(test@xen650901.gplhost.com): Disconnected: Logged out top=0/0, retr=1/4047, del=0/1, size=4030
	if ( strstr(words[5], "POP3(") ){
//		syslog(LOG_NOTICE, "Found pop");
		bytes_fld = strstr(words[10],"retr=");
		if(bytes_fld == NULL){
//			syslog(LOG_NOTICE,"Didn't found retr=");
			free(user_str);
			return 0;
		}
		bytes_fld += strlen("retr=");
		bytes_fld = strstr(words[10],"/");
		if(bytes_fld == NULL){
//			syslog(LOG_NOTICE,"Didn't found slash");
			free(user_str);
			return 0;
		}
		bytes_fld = bytes_fld + 1;
		mysqmail_bytes += atoi(bytes_fld);

		sprintf(query,"INSERT IGNORE INTO %s (domain_name,month,year) VALUES ('%s','%s','%s');"
			,mysqmail_config.mysql_table_scoreboard, domain, sql_month, sql_year);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
			free(user_str);
			return 0;
		}
		sprintf(query,"UPDATE %s SET pop_trafic=pop_trafic+%ld WHERE domain_name='%s' AND month='%s' AND year='%s'"
			,mysqmail_config.mysql_table_scoreboard, mysqmail_bytes, domain, sql_month, sql_year);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
			free(user_str);
			return 0;
		}
		sprintf(query,"UPDATE %s SET pop3_login_count=pop3_login_count+1 , pop3_transfered_bytes=pop3_transfered_bytes+%ld,last_login=%ld WHERE id='%s' AND mbox_host='%s';", mysqmail_config.mysql_table_pop_access,mysqmail_bytes, (long) tv.tv_sec,user,domain);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
		}
	// Aug 20 17:25:12 mx dovecot: IMAP(test@xen650901.gplhost.com): Disconnected: Logged out bytes=464/1270
	} else if ( strstr(words[5], "IMAP(") ){
//		syslog(LOG_NOTICE, "Found imap");
		bytes_fld = strstr(words[9],"bytes=");
		if(bytes_fld == NULL){
//			syslog(LOG_NOTICE,"Didn't found bytes=");
			free(user_str);
			return 0;
		}
		bytes_fld = bytes_fld + strlen("bytes=");
		mysqmail_bytes += atoi(bytes_fld);
		bytes_fld = strstr(bytes_fld,"/");
		if( bytes_fld != NULL){
//			syslog(LOG_NOTICE,"Didn't found slash");
			bytes_fld++;
			mysqmail_bytes += atoi(bytes_fld);
		}
		
		sprintf(query,"INSERT IGNORE INTO %s (domain_name,month,year) VALUES ('%s','%s','%s');"
			,mysqmail_config.mysql_table_scoreboard, domain, sql_month, sql_year);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
			free(user_str);
			return 0;
		}
		sprintf(query,"UPDATE %s SET imap_trafic=imap_trafic+%ld \
			WHERE domain_name='%s' AND month='%s' AND year='%s'"
			,mysqmail_config.mysql_table_scoreboard, mysqmail_bytes, domain, sql_month, sql_year);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
			free(user_str);
			return 0;
		}
		sprintf(query,"UPDATE %s SET imap_login_count=imap_login_count+1 , imap_transfered_bytes=imap_transfered_bytes+%ld,last_login=%ld WHERE id='%s' AND mbox_host='%s';", mysqmail_config.mysql_table_pop_access,mysqmail_bytes, (long) tv.tv_sec,user,domain);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
		}
	}
	free(user_str);
	return 0;
}

int main(int argc, char **argv){
	int ret;
	daemonize();
	setlogmask (LOG_UPTO (LOG_NOTICE));
	openlog(DAEMON_NAME, LOG_NDELAY | LOG_CONS | LOG_PID, LOG_LOCAL1);
	write_pidfile("mysqmail-dovecot-logger");
	reg_hand();

	read_config_file();
	do_ze_mysql_connect();
	ret = log_all_lines("syslog",&log_a_line);
	mysql_close(sock);
	return ret;
}
