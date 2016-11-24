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

#define DAEMON_NAME "mysqmail-q-logger"

int extern errno;
extern char **environ;
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

/////////////////////////////////////////////////
// Config file reader stuff: using dotconf lib //
/////////////////////////////////////////////////
mysqmail_config_t mysqmail_config;

/////////////////////////
// Mysql connect stuff //
/////////////////////////
MYSQL mysql,*sock;
MYSQL_RES *res;

void cleanup_all_recs(){
	char query[256]="";
	sprintf(query,"DELETE FROM %s WHERE newmsg_id=NULL;", mysqmail_config.mysql_table_smtp_logs);
	if(mysql_query(sock,query)){
		syslog(LOG_NOTICE, "Query: \"%s\" failed ! %s\n",query,mysql_error(sock));
		exit(2);
	}
	syslog(LOG_NOTICE, "qmail-logger connected to MySQL !\n");
}

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

#define SKIP_WORD 6

typedef struct{
	long tv_sec;
	long tv_usec;
}timeval_t;

int log_a_line(char* logline){
/*	Log lines sent by qmail should have the following format, as
	the qmail-log man tels...
	new msg MSG_NUMBER
	info msg MSG_NUMBER: bytes NUMBER from SENDER [...]
	starting delivery D_NUMBER: msg MSG_NUMBER to [local/remote] DELIVERY_EMAIL
	delivery D_NUMBER: success:
	delivery D_NUMBER: failure:
	delivery D_NUMBER: deferral:
	bounce msg MSG_NUMBER
	triple bounce: discarding bounce/MSG_NUMBER
	end msg MSG_NUMBER
*/
	char* words[MAX_LOG_WORDS+1];
	char query[MAX_QUERY_SIZE+1]="";
	char* delivery_id;
	char* sender_user;
	char* sender_domain;
	char* delivery_user;
	char* delivery_domain;
	char* msg_id;
	char* cur_p;
	char* tmp;
	int num_words=0;
	char* smtp_logs;
	char ds[1024];
	char* ds2;
//	int i;

	struct timeval tv;

	smtp_logs = mysqmail_config.mysql_table_smtp_logs;

	// Tokenise the line in words
	ds2 = strdup(logline);
	cur_p = strtok(logline," ");
	while( num_words < MAX_LOG_WORDS && (words[num_words++] = cur_p) != NULL){
		cur_p = strtok(NULL," ");
	}
	num_words--;

	sprintf(ds,"Found %d words: %s",num_words,ds2);

	strcpy(query,"");
//	sprintf(query,"");

	// new msg MSG_NUMBER
	if(num_words <= SKIP_WORD || !strstr(words[4], "qmail") ||
		!strcmp(words[SKIP_WORD],"new")){
		strcpy(query,"");
		sprintf(ds,"Founded not parsable log line.");

	// info msg MSG_NUMBER: bytes NUMBER from SENDER [...]
	}else if(!strcmp(words[SKIP_WORD],"info") && !strcmp(words[SKIP_WORD+1],"msg")){

		sprintf(ds,"Founded info");

		msg_id = words[SKIP_WORD+2];
		msg_id[ (strlen(msg_id)-1) ] = 0;
		sender_user = words[SKIP_WORD+6];

		// Try to detect bounced or double-bounced messages by sender addr (don't know if it's the good way...)
		if(!strcmp(sender_user,"<>") || !strcmp(sender_user,"<#@[]>")){
			sprintf(query,"UPDATE %s SET bytes='%s',newmsg_id='%s' WHERE bounce_qp='%s';",
							smtp_logs,words[SKIP_WORD+4],msg_id,words[SKIP_WORD+8]);
		}else{
			sender_user++;
			sender_domain = strstr(sender_user,"@");
			if(sender_domain != NULL){
				*sender_domain++ = 0;
				sender_domain[ (strlen(sender_domain)-1) ] = 0;
				gettimeofday(&tv,NULL);
				sprintf(query,"INSERT INTO %s (time_stamp,newmsg_id,bytes,sender_user,sender_domain) VALUES('%ld','%s','%s','%s','%s')",
									smtp_logs,tv.tv_sec,words[SKIP_WORD+2],words[SKIP_WORD+4],sender_user,sender_domain);
			}
		}
	// starting delivery D_NUMBER: msg MSG_NUMBER to local domain-com-user@domain.com
	// starting delivery D_NUMBER: msg MSG_NUMBER to remote user@domain.com
	}else if(!strcmp(words[SKIP_WORD+0],"starting") && !strcmp(words[SKIP_WORD+1],"delivery")){
		sprintf(ds,"Founded statring delivery %s",words[SKIP_WORD+2]);
		delivery_id = words[SKIP_WORD+2];
		delivery_id[strlen(delivery_id)-1] = 0;	// Remove the :
		delivery_user = words[SKIP_WORD+7];
		delivery_domain = strstr(delivery_user,"@");	// Separate domain & user
		*delivery_domain++ = 0;
		if(!strcmp(words[SKIP_WORD+6],"local")){
			// If to local, virtual domains does domain-com-user@domain.com
			// so we have to remove domain-com- from the username...
			if(strlen(delivery_user)>strlen(delivery_domain)){
				char tmp2[256];
				char* c__p;
				strncpy(tmp2,delivery_domain,254);
				tmp2[254]=0;
				c__p = &tmp2[0];
				while(*c__p){
					if(*c__p == '.')	*c__p = '-';
					c__p++;
				}
				*c__p++ = '-';
				*c__p++ = 0;
				if(!strncmp(delivery_user,tmp2,strlen(tmp2))){
					delivery_user += strlen(tmp2);
				}
			}
			sprintf(query,"UPDATE %s SET delivery_id='%s',delivery_user='%s',delivery_domain='%s' WHERE newmsg_id='%s';",
			smtp_logs,delivery_id,delivery_user,delivery_domain,words[SKIP_WORD+4]);
		}else{
			sprintf(query,"UPDATE %s SET delivery_id='%s',delivery_user='%s',delivery_domain='%s' WHERE newmsg_id='%s';",
			smtp_logs,delivery_id,delivery_user,delivery_domain,words[SKIP_WORD+4]);
		}
	// delivery D_NUMBER:
	}else if(!strcmp(words[SKIP_WORD+0],"delivery")){
		sprintf(ds,"Founded delivery #%s",words[SKIP_WORD+1]);
		delivery_id = words[SKIP_WORD+1];
		delivery_id[strlen(delivery_id)-1] = 0;
		// delivery D_NUMBER: success:
		if(!strcmp(words[SKIP_WORD+2],"success:")){
			sprintf(query,"UPDATE %s SET delivery_success='yes',delivery_id=NULL WHERE delivery_id='%s';",
							smtp_logs,delivery_id);
		// delivery D_NUMBER: failure:
		}else if(!strcmp(words[SKIP_WORD+2],"failure:")){
		// delivery D_NUMBER: deferral:
		}else if(!strcmp(words[SKIP_WORD+2],"deferral:")){
		}else{
		}
	// bounce msg 236400 qp 16889
	}else if(!strcmp(words[SKIP_WORD+0],"bounce")){
		sprintf(ds,"Founded bounce #%s",words[SKIP_WORD+4]);
		sprintf(query,"UPDATE %s SET bounce_qp='%s' WHERE newmsg_id='%s';",
							smtp_logs,words[SKIP_WORD+4],words[SKIP_WORD+2]);
	// triple bounce: discarding bounce/MSG_NUMBER
	}else if(!strcmp(words[SKIP_WORD+0],"triple")){
		sprintf(ds,"Founded tripple bounce #%s",words[SKIP_WORD+3]);
		tmp = words[SKIP_WORD+3];
		tmp += strlen("bounce/");
		sprintf(query,"DELETE FROM %s WHERE newmsg_id='%s';",
						smtp_logs,tmp);
	// end msg MSG_NUMBER
	}else if(!strcmp(words[SKIP_WORD+0],"end")){
		MYSQL_RES *res;
		MYSQL_ROW row;
		char* bytes;
		char* sender_user;
		char* sender_domain;
		char* delivery_user;
		char* delivery_domain;
		int num_rows;

		// Message delivery finished, we should get infos and log them in the scoreboard
		sprintf(query,"SELECT * FROM %s WHERE newmsg_id='%s';",
							smtp_logs,words[SKIP_WORD+2]);
		
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed ! %s\n",query,mysql_error(sock));
			return 1;
		}
		if (!(res=mysql_store_result(sock))){
			syslog(LOG_NOTICE, "Couldn't get result from %s\n",mysql_error(sock));
			return 1;
		}
		num_rows = mysql_num_rows(res);
		if(num_rows == 1){
			row = mysql_fetch_row(res);
			if(row == NULL){
				syslog(LOG_NOTICE, "Couldn't fetch row: %s\n",mysql_error(sock));
			}else{
				struct tm *ptr;
				time_t tm;
				char sql_month[60];
				char sql_year[60];

				tm = time(NULL);
				ptr = localtime(&tm);
				strftime(sql_month ,60 , "\%m",ptr);
				strftime(sql_year ,60 , "\%Y",ptr);

				bytes = strdup(row[3]);
				sender_user = strdup(row[4]);
				sender_domain = strdup(row[5]);
				delivery_user = strdup(row[7]);
				delivery_domain = strdup(row[8]);
				// Check to see if one of the domain is hosted here
				// First check sender domain
				sprintf(query,"SELECT name FROM %s WHERE name='%s'"
					,mysqmail_config.mysql_table_domain, sender_domain);
				if(mysql_query(sock,query)){
					syslog(LOG_NOTICE, "Query: \"%s\" failed ! %s\n",query,mysql_error(sock));
					return 1;
				}
				if (!(res=mysql_store_result(sock))){
					syslog(LOG_NOTICE, "Couldn't get result from %s\n",mysql_error(sock));
					return 1;
				}
				num_rows = mysql_num_rows(res);
				if(num_rows == 1){
					sprintf(query,"INSERT IGNORE INTO %s (domain_name,month,year) VALUES ('%s','%s','%s');"
						,mysqmail_config.mysql_table_scoreboard, sender_domain, sql_month, sql_year);
					if(mysql_query(sock,query)){
						syslog(LOG_NOTICE, "Query: \"%s\" failed ! %s\n",query,mysql_error(sock));
						return 1;
					}

					sprintf(query,"UPDATE %s SET pop_trafic=pop_trafic+%s \
						WHERE domain_name='%s' AND month='%s' AND year='%s'"
						,mysqmail_config.mysql_table_scoreboard, bytes, sender_domain, sql_month, sql_year);
					if(mysql_query(sock,query)){
						syslog(LOG_NOTICE, "Query: \"%s\" failed ! %s\n",query,mysql_error(sock));
						return 1;
					}
				}

				// Then check reciever domain
				sprintf(query,"SELECT name FROM %s WHERE name='%s'"
					,mysqmail_config.mysql_table_domain, delivery_domain);
				if(mysql_query(sock,query)){
					syslog(LOG_NOTICE, "Query: \"%s\" failed ! %s\n",query,mysql_error(sock));
					return 1;
				}
				if (!(res=mysql_store_result(sock))){
					syslog(LOG_NOTICE, "Couldn't get result from %s\n",mysql_error(sock));
					return 1;
				}
				num_rows = mysql_num_rows(res);
				if(num_rows == 1){
					sprintf(query,"INSERT IGNORE INTO %s (domain_name,month,year) VALUES ('%s','%s','%s');"
						,mysqmail_config.mysql_table_scoreboard, delivery_domain, sql_month, sql_year);
					if(mysql_query(sock,query)){
						syslog(LOG_NOTICE, "Query: \"%s\" failed ! %s\n",query,mysql_error(sock));
						return 1;
					}

					sprintf(query,"UPDATE %s SET pop_trafic=pop_trafic+%s \
						WHERE domain_name='%s' AND month='%s' AND year='%s'"
						,mysqmail_config.mysql_table_scoreboard, bytes, delivery_domain, sql_month, sql_year);
					if(mysql_query(sock,query)){
						syslog(LOG_NOTICE, "Query: \"%s\" failed ! %s\n",query,mysql_error(sock));
						return 1;
					}
				}
			}
		}
		mysql_free_result(res);
		// Delete the message from the log table
		sprintf(query,"DELETE FROM %s WHERE newmsg_id='%s';",
							smtp_logs,words[SKIP_WORD+2]);
	// Message not recognised: send to next piped logger (syslog?)
	}else{
	}

//	fprintf(stdout,"Query: \"%s\" !\n",query);
	// Issue the query return from function.
	if(strlen(query)!=0){
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed ! %s\n",query,mysql_error(sock));
		}
	}
	return 0;
}

int main(int argc, char **argv){
	int ret;
	daemonize();
	setlogmask (LOG_UPTO (LOG_NOTICE));
	openlog(DAEMON_NAME, LOG_NDELAY | LOG_CONS | LOG_PID, LOG_LOCAL1);
	write_pidfile("mysqlmail-qmail-logger");
	reg_hand();

	read_config_file();
	do_ze_mysql_connect();
	cleanup_all_recs();
	ret = log_all_lines("syslog",&log_a_line);
	mysql_close(sock);
	return ret;
}
