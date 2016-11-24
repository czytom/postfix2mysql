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
#include <dotconf.h>
#include <limits.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>

#include "mydaemon.h"
#include "myconfig.h"

#define DAEMON_NAME "mysqmail-pure-ftpd-logger"

int extern errno;
extern char **environ;
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

mysqmail_config_t mysqmail_config;

/////////////////////////
// Mysql connect stuff //
/////////////////////////
MYSQL mysql,*sock;
MYSQL_RES *res;

///////////////////////////////////////////////////////////////////
// A line of log must be parsed and sent to correct mysql record //
///////////////////////////////////////////////////////////////////
// Here is a standard qmail log file thrue syslog:
// Feb 24 12:51:15 www qmail: 1109249475.344018 bounce msg 6144008 qp 22053
// The parser will then skip 6 words (from "Feb" to the microtime)
// and start analysing at "bounce", so we set SKIP_WORD to 6.
// We will also check that the message is fom "qmail:", if not, then it's
// another daemon message...
#define MAX_LOG_WORDS 1024
#define EMAIL_ADDR_SIZE 256
#define MAX_QUERY_SIZE 1024
#define MAX_LOGLINE_SIZE 2048
#define SKIP_WORD 6

typedef struct{
	long tv_sec;
	long tv_usec;
}timeval_t;

void log_to_domain_table(char* transfered_bytes,char* domain_name){
	char query[1024]="";
	static char sql_month[60];
	static char sql_year[60];
	struct tm *ptr;
	time_t tm;

	tm = time(NULL);
	ptr = localtime(&tm);
	strftime(sql_month ,60 , "\%m",ptr);
	strftime(sql_year ,60 , "\%Y",ptr);

	sprintf(query,"INSERT IGNORE INTO %s (id,sub_domain,transfer,hits,month,year) VALUES ('','%s','0','0',%s,%s)",
		"ftp_accounting",domain_name,sql_month,sql_year);
	if(mysql_query(sock,query)){
		syslog(LOG_ERR, "Query: \"%s\" failed: %s",query,mysql_error(sock));
		return;
	}
	sprintf(query,"UPDATE %s SET transfer=transfer+%s,hits=hits+1 WHERE sub_domain='%s' AND month='%s' AND year='%s'",
		"ftp_accounting",transfered_bytes,domain_name,sql_month,sql_year);
	if(mysql_query(sock,query)){
		syslog(LOG_ERR, "Query: \"%s\" failed: %s",query,mysql_error(sock));
		return;
	}
}

int log_a_line(char* logline){
	char* words[MAX_LOG_WORDS+1];
	char query[MAX_QUERY_SIZE+1]="";
	char* cur_p;
	int num_words=0;
	char* smtp_logs;
//	static char ds[MAX_LOGLINE_SIZE+2];
	static char logline_cpy[MAX_LOGLINE_SIZE+2];
	long num_rows;
	MYSQL_ROW row;


	MYSQL_RES *res;

	smtp_logs = mysqmail_config.mysql_table_smtp_logs;

	if( strlen(logline) > MAX_LOG_WORDS ){
		syslog(LOG_ERR, "Log line bigger than buffer size: skipping line");
	}
	// Tokenise the line in words
	strcpy(logline_cpy,logline);
	cur_p = strtok(logline," ");
	while( num_words < MAX_LOG_WORDS && (words[num_words++] = cur_p) != NULL){
		cur_p = strtok(NULL," ");
	}
	num_words--;

	if (num_words < 5) {
		return 1;
	}

	strcpy(query,"");

// Start of gramar cheks
	// Handle anonymous logins differently!
	if( strcmp(words[2],"ftp") == 0 || strcmp(words[2],"anonymous") == 0 ){
		syslog(LOG_ERR, "Anonymous login: %s bytes",words[num_words-1]);
	}else{
		//3rd word should be the user name
		sprintf(query,"SELECT hostname FROM %s WHERE login='%s';","ftp_access",words[2]);
		if(mysql_query(sock,query)){
			syslog(LOG_ERR, "Query: \"%s\" failed line %d: %s",query,__LINE__,mysql_error(sock));
			return 1;
		}
		if (!(res=mysql_store_result(sock))){
			syslog(LOG_ERR, "Couldn't get result: %s",mysql_error(sock));
			return 1;
		}
		num_rows = mysql_num_rows(res);
		if(num_rows == 1){
			row = mysql_fetch_row(res);
			if(row == NULL){
				mysql_free_result(res);
				return 1;
			}
			if( strcmp("\"GET",words[5]) == 0 ){
				// Last word should be the number of bytes transfered
				sprintf(query,"UPDATE %s SET dl_count=dl_count+1, dl_bytes=dl_bytes+%s WHERE login='%s'","ftp_access",words[num_words-1],words[2]);
				log_to_domain_table(words[num_words-1],row[0]);
			}else if( strcmp("\"PUT",words[5]) == 0 ){
				sprintf(query,"UPDATE %s SET ul_count=dl_count+1, ul_bytes=ul_bytes+%s WHERE login='%s'","ftp_access",words[num_words-1],words[2]);
				log_to_domain_table(words[num_words-1],row[0]);
			}else{
				strcpy(query,"");
				syslog(LOG_ERR, "Commande %s not understood",words[5]);
			}
		}else{
			strcpy(query,"");
			syslog(LOG_ERR, "User %s not found in table!",words[2]);
		}
		mysql_free_result(res);
	}

// End of gramar cheks

//	fprintf(stdout,"Query: \"%s\" !\n",query);
	// Issue the query return from function.
	if(strlen(query)!=0){
		if(mysql_query(sock,query)){
			syslog(LOG_ERR, "Query: \"%s\" failed: %s",query,mysql_error(sock));
		}
	}
	return 0;
}

int main(int argc, char **argv){
	int ret;
	// if (argc > 1 && !strcmp(argv[1],"-D")) daemonize();
	daemonize();
	setlogmask (LOG_UPTO (LOG_NOTICE));
	openlog("mysqmail-pur-logger", LOG_NDELAY | LOG_CONS | LOG_PID, LOG_LOCAL1);
	syslog(LOG_NOTICE, "mysqmail-pure-ftpd-logger started");
	write_pidfile("mysqmail-pure-ftpd-logger");
	reg_hand();

	read_config_file();
	do_ze_mysql_connect();
	// Manages locations of the log file for both CentOS and Debian.
	if( access("/var/log/pure-ftpd/transfer.log", R_OK) == 0){
		ret = log_all_lines("/var/log/pure-ftpd/transfer.log",&log_a_line);
	}else{
		ret = log_all_lines("/var/log/pureftpd.log",&log_a_line);
	}
	mysql_close(sock);
	return ret;
}
