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

//#include "mydaemon.h"
#include "myconfig.h"
#include "mydaemon.h"

#define DAEMON_NAME "mysqmail-post-logger"

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

void cleanup_old_recs (){
	char query[256]="";
	sprintf(query,"DELETE FROM %s WHERE newmsg_id=NULL;",
			mysqmail_config.mysql_table_smtp_logs);
	if(mysql_query(sock,query)){
		syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
	}
	syslog(LOG_INFO, "Connected to MySQL!");
	return;
}

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


int parseEmail(char* source,char** domain,char** user){
	char *domain_tmp;
	int i;
	if(source[0] == '<' && source[1] == '>'){
		*domain = NULL;
		*user = NULL;
		return 1;
	}
	if(source == NULL)	return 0;
	if(source[0] == '<')
		source ++;
	i = strlen(source);
	if(i > 128)	return 0;

	while ( (source[i-1] == '>') && i > 2){
		i--;
	}
	source[i] = 0;

	domain_tmp = strstr(source,"@");
	if(domain_tmp == NULL)	return 0;	// There must be an @ sign
	if(strlen(domain_tmp) < 4)	return 0;	// and at least 4 chars after

	*domain_tmp++ = 0;

	if(strlen(source) < 1)	return 0;
	*domain = domain_tmp;
	*user = source;
	return 1;
}

int log_a_line(char* logline){
/*	Log lines sent by postfix should have the following format
	Mar 16 00:00:08 <hostname> postfix/smtpd[<pid>]: connect from <hostname>[<ip>] 
	Mar 16 00:00:09 <hostname> postfix/smtpd[<pid>]: NOQUEUE: reject: RCPT from <hostname>[<ip>]
	Mar 16 00:00:11 <hostname> postfix/smtpd[<pid>]: disconnect from <hostname>[<ip>]
	Mar 16 00:01:25 <hostname> postfix/smtpd[<pid>]: connect from <hostname>[<ip>]
	Mar 16 00:01:28 <hostname> postfix/smtpd[<pid>]: <queue id>: client=<hostname>[<ip>]
	Mar 16 00:01:29 <hostname> postfix/cleanup[<pid>]: <queue id>: message-id=
	Mar 16 00:01:29 <hostname> postfix/qmgr[<pid>]: <queue id>: from=<email address>, size=<message size>, nrcpt=1 (queue active) 
	Mar 16 00:01:29 <hostname> postfix/local[<pid>]: <queue id>: to=<email address>, relay=local, delay=4, status=sent (delivered to command: /usr/bin/procmail) 
	Mar 16 00:01:29 <hostname> postfix/qmgr[<pid>]: <queue id>: removed 
	Mar 16 00:01:29 <hostname> postfix/smtpd[<pid>]: disconnect from <hostname>[<ip>]*/
	char* words[MAX_LOG_WORDS+1];
	char query[MAX_QUERY_SIZE+1]="";
	char* delivery_id=NULL;
	char* sender_user=NULL;
	char* sender_domain=NULL;
	char* delivery_user=NULL;
	char* delivery_domain=NULL;
	char* cur_p=NULL;
	char* end=NULL;
	char* bytes=NULL;
	int num_words=0,parentesis;
	char* smtp_logs=NULL;
	char* scoreboard=NULL;
	char* domain=NULL;
	int i,j,ret;

#define M_FROM 0
#define M_SIZE 1
#define M_NRCPT 2
#define M_ORIGTO 3
#define M_CLIENT 4
#define M_RELAY 5
#define M_DELAYS 6
#define M_DELAY 7
#define M_STATUS 8
#define M_TO 9
#define M_DSN 10
#define M_MSGID 11
#define NUM_KEYWORDS 12
	char *keywords[]= {"from", "size", "nrcpt", "orig_to", "client", "relay", "delays", "delay", "status", "to", "dsn", "message-id"};
	char *values[NUM_KEYWORDS+1];
	char *tokenizer;

	smtp_logs = mysqmail_config.mysql_table_smtp_logs;
	scoreboard = mysqmail_config.mysql_table_scoreboard;
	domain = mysqmail_config.mysql_table_domain;


	// Tokenise the line in words
	tokenizer = " ";
	num_words = 0;
	end = logline + strlen(logline);
	words[num_words++] = logline;
	cur_p = strtok(logline,tokenizer);
	while(num_words<6 && cur_p != NULL){
		cur_p = strtok(NULL,tokenizer);
		words[num_words] = cur_p;
//		fprintf(stdout,"Word %d: \"%s\"\n",num_words,cur_p);
		num_words++;
	}
	cur_p += strlen(cur_p) + 1;
	while(num_words < MAX_LOG_WORDS && cur_p != NULL && cur_p < end){
		if( *cur_p == ','){
			*cur_p++ = '\0';
		}
		if( *cur_p == ' '){
			*cur_p++ = '\0';
		}
		words[num_words] = cur_p;
		parentesis = 0;
		while(cur_p < end && (*cur_p != ',' || parentesis > 0)){
//			fprintf(stdout,"Word %d: \"%s\"\n",num_words,cur_p);
			if(*cur_p == '('){
				parentesis++;
			}
			if(*cur_p == ')'){
				parentesis--;
			}
			cur_p++;
		}
		num_words++;
	}

//	for(i=0;i<num_words;i++){
//		fprintf(stdout,"Word %d: \"%s\"\n",i,words[i]);
//	}

	if( num_words < 6 ){
//		fprintf(stdout,"Not enough logged words\n");
		return 0; // not enough data on line
	}
	if ( ! strstr(words[4], "postfix") && ! strstr(words[4], "vmailer")){
//		fprintf(stdout,"Not a postfix line: %s\n",words[4]);
		return 0; // this isn't a postfix line!
	}

	if( strstr(words[5], "timeout") || strstr(words[5], "disconnect") || strstr(words[5], "NOQUEUE:") || strstr(words[5], "lost") || strstr(words[5], "connect") || strstr(words[5], "warning:") || strstr(words[5], "discarding") ){
//		fprintf(stdout,"Not interested by this postfix line\n");
		return 0; // we are not interested by this kind of log lines
	}

	values[M_FROM] = NULL;
	values[M_SIZE] = NULL;
	values[M_NRCPT] = NULL;
	values[M_TO] = NULL;
	values[M_CLIENT] = NULL;
	values[M_RELAY] = NULL;
	values[M_DELAY] = NULL;
	values[M_DELAYS] = NULL;
	values[M_STATUS] = NULL;
	values[M_ORIGTO] = NULL;
	values[M_DSN] = NULL;
	values[M_MSGID] = NULL;

//	fprintf(stdout,"Headding: \"%s\", numwords: %d\n",words[5],num_words);
	// *** Parse all words in a key / value array ***
	if( num_words > 6 ){
		for(i=6;i<num_words;i++){
			// For all tokenised words
			cur_p = words[i];
			for(j=0;j<NUM_KEYWORDS;j++){
				// compare with our keyword list, if matches, store
				if( strncmp(cur_p, keywords[j],strlen(keywords[j]))==0){
					cur_p += strlen(keywords[j]) + 1;
					values[j] = cur_p;
//					fprintf(stdout,"Word: %d, keyword: \"%s\" value: \"%s\"\n",i,keywords[j],cur_p);
				}
			}
		}
	}

	//sprintf(query,"");

	//----new ID appear on lines like the following:

	// 0 1 2 3 postfix/pickup[17425]: 04D341007DA9
	// 0 1 2 3 postfix/smtpd[17495]: E8A8E1007DA9: client=
	// 0 1 2 3 postfix/smtpd[2919]: E0AB117E26: client=
	// 0 1 2 3 postfix/smtpd[6333]: A4A4A17E2B: client=

	//----new from and size details appear like this:

	// 0 1 2 3 postfix/qmgr[17116]: 04D341007DA9: from=<root@mx.new.tusker.net>, size=286, nrcpt=1 (queue active)
	// 0 1 2 3 postfix/qmgr[17116]: E8A8E1007DA9: from=<tusker@ordo.cable.nu>, size=569, nrcpt=1 (queue active)
	// 0 1 2 3 postfix/qmgr[1108]: E0AB117E26: from=<>, size=8507, nrcpt=1 (queue active)
	// 0 1 2 3 postfix/qmgr[1108]: A4A4A17E2B: from=<sans@sans.org>, size=25573, nrcpt=1 (queue active)

	//----new to details appear like this:

	// 0 1 2 3 postfix/virtual[17491]: 04D341007DA9: to=<tester@new.tusker.net>, relay=virtual, delay=0, status=sent (maildir)
	// 0 1 2 3 postfix/virtual[17491]: E8A8E1007DA9: to=<tester@new.tusker.net>, relay=virtual, delay=0, status=sent (maildir)
	// 0 1 2 3 postfix/smtp[3191]: E0AB117E26: to=<adam@fission.tusker.net>, orig_to=<zboszor@ali.as>, relay=127.0.0.1[127.0.0.1], delay=2, status=sent (250 2.6.0 Ok, id=03278-02, from MTA: 250 Ok: queued as F0BF617E30)

	//----reject message
	// 0 1 2 3 postfix/smtpd[2410]: A1CE861A83: reject: RCPT from unknown[218.246.34.68]: 557 <reason>
	//----bounce message
	// 0 1 2 3 postfix/local[6334]: A4A4A17E2B: to=<dee@fission.tusker.net>, relay=local, delay=1, status=bounced (unknown user: "dee")

	// *** Do basic checks on the queue ID, then store it in the delivery_id var ***
	cur_p = words[5];
	if( (strlen(cur_p) != 12 && strlen(cur_p) != 11) || (cur_p[11] != ':' && cur_p[10] != ':' )){
//		fprintf(stdout,"postfix id not right!");
		return 0;
	}
	cur_p[11] = '\0';
	delivery_id = cur_p;

	if(values[M_FROM] != NULL && values[M_SIZE] != NULL){
		if(*values[M_SIZE] == '\0'){
			return 0; // If size is zero, then we really don't care
		}
		ret = parseEmail(values[M_FROM],&sender_user,&sender_domain);
		if(sender_user == NULL || sender_domain == NULL){
			return 0; // If we don't even have a domain, who cares?
		}
		sprintf(query,"INSERT INTO %s (sender_user, sender_domain, bytes, delivery_id_text) VALUES ('%s','%s','%s','%s');", smtp_logs, sender_user, sender_domain, values[M_SIZE], delivery_id);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
		}
		return 0;
	}else if(values[M_TO] != NULL && values[M_STATUS] != NULL){
		MYSQL_RES *res=NULL;
		MYSQL_ROW row;
		int num_rows;
		char sql_month[60];
		char sql_year[60];
		struct tm *ptr;
		time_t tm;
		char* dom_to_search;

		if( strncmp(values[M_STATUS], "sent",strlen("sent")) !=0){
			return 0; // We account only sent messages (which include remotely sent and delivered by maildrop)
		}
		ret = parseEmail(values[M_TO],&delivery_domain,&delivery_user);
		if(ret == 0)    return 1;

		// Get current date
		tm = time(NULL);
		ptr = localtime(&tm);
		strftime(sql_month ,100 , "\%m",ptr);
		strftime(sql_year ,100 , "\%Y",ptr);

		// Get the domain name of the sender and the number of bytes from previous saved SQL record
		sprintf(query,"SELECT bytes, sender_domain FROM %s WHERE delivery_id_text='%s'"
			, smtp_logs, delivery_id);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
			return 0;
		}
		if (!(res=mysql_store_result(sock))){
			syslog(LOG_NOTICE, "Couldn't get result from %s\n",mysql_error(sock));
			return 0;
		}
		num_rows = mysql_num_rows(res);
		if(num_rows != 1){
			mysql_free_result(res);
			return 0;
		}
		row = mysql_fetch_row(res);
		if(row == NULL){
			syslog(LOG_NOTICE, "Couldn't fetch row: %s\n",mysql_error(sock));
			if(res != NULL){
				mysql_free_result(res);
				res = NULL;
			}
			return 0;
		}

		sender_domain = strdup(row[1]);
		bytes = strdup(row[0]);
		if(res != NULL){
			mysql_free_result(res);
			res = NULL;
		}

		// Maybe we are delivering to a local mailbox,
		// and we should account to a local domain for delivery
		// So we check if the domain name we founded is hosted here
		dom_to_search = delivery_domain;
		sprintf(query,"SELECT owner,name FROM %s WHERE name='%s'",
			domain, dom_to_search);
		if(mysql_query(sock,query)){
			syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
			return 0;
		}
		if (!(res=mysql_store_result(sock))){
			syslog(LOG_NOTICE, "Couldn't get result: %s",mysql_error(sock)); 
			if(res != NULL){
				mysql_free_result(res);
				res = NULL;
			}
			return 0;
		}

		num_rows = mysql_num_rows(res);
		if(res != NULL){
			mysql_free_result(res);
			res = NULL;
		}
		if(num_rows == 1){
			// Update the accounting tables
			sprintf(query,"INSERT IGNORE INTO %s (domain_name,month,year) VALUES ('%s','%s','%s')"
				, scoreboard, dom_to_search, sql_month, sql_year);
			if(mysql_query(sock,query)){
				syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
			}
			sprintf(query,"UPDATE %s SET smtp_trafic=smtp_trafic+%s WHERE domain_name='%s' AND month='%s' AND year='%s';",
				scoreboard, bytes, dom_to_search, sql_month, sql_year);
			if(mysql_query(sock,query)){
				syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
			}
		}else{
			// It seems it's a mail for the outside. Try to guess if the from= is in our database
			dom_to_search = sender_domain;

			sprintf(query,"SELECT owner,name FROM %s WHERE name='%s'",
				domain, dom_to_search);
			if(mysql_query(sock,query)){
				syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
				return 0;
			}
			if (!(res=mysql_store_result(sock))){
				syslog(LOG_NOTICE, "Couldn't get result from %s\n",mysql_error(sock)); 
				if(res != NULL){
					mysql_free_result(res);
					res = NULL;
				}
				return 0;
			}
				
			num_rows = mysql_num_rows(res);
			if(res != NULL){
				mysql_free_result(res);
				res = NULL;
			}
			if(num_rows == 1){
				// Update the accounting tables
				sprintf(query,"INSERT IGNORE INTO %s (domain_name,month,year) VALUES ('%s','%s','%s')"
					, scoreboard, dom_to_search, sql_month, sql_year);
				if(mysql_query(sock,query)){
					syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
				}
				sprintf(query,"UPDATE %s SET smtp_trafic=smtp_trafic+%s WHERE domain_name='%s' AND month='%s' AND year='%s';",
					scoreboard, bytes, dom_to_search, sql_month, sql_year);
				if(mysql_query(sock,query)){
					syslog(LOG_NOTICE, "Query: \"%s\" failed: %s",query,mysql_error(sock));
				}
			}
		}
		if(sender_domain != NULL){
			free(sender_domain);
			sender_domain = NULL;
		}
		if(bytes != NULL){
			free(bytes);
			bytes = NULL;
		}

	}else if(strstr(words[6],"removed") ){
		sprintf(query,"DELETE FROM %s WHERE delivery_id_text='%s'", smtp_logs, delivery_id);
		if(mysql_query(sock,query)){
			fprintf(stdout,"Query: \"%s\" failed ! %s\n",query,mysql_error(sock));
		}
	}
	return 0;
}

int main(int argc, char **argv){
	int ret;
	daemonize();
	setlogmask (LOG_UPTO (LOG_NOTICE));
	openlog(DAEMON_NAME, LOG_NDELAY | LOG_CONS | LOG_PID, LOG_LOCAL1);
	syslog (LOG_NOTICE, "Program started by User %d", getuid ());
	write_pidfile("mysqmail-postfix-logger");
	reg_hand();

	read_config_file();
	do_ze_mysql_connect();
	cleanup_old_recs();
	ret = log_all_lines("syslog",&log_a_line);
	mysql_close(sock);
	return ret;
}
