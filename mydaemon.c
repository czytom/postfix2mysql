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
#include <mysql/mysql.h>

#include "mydaemon.h"
#include "myconfig.h"

// Filename of the pid
static char pid_fn[8096];

extern MYSQL mysql,*sock;
extern mysqmail_config_t mysqmail_config;
int do_ze_mysql_connect(){
	mysql_init(&mysql);
	if (!(sock = mysql_real_connect(&mysql,mysqmail_config.mysql_hostname,
		mysqmail_config.mysql_user,mysqmail_config.mysql_pass,mysqmail_config.mysql_db,0,NULL,0))){
		syslog(LOG_NOTICE, "Couldn't connect to engine: %s",mysql_error(&mysql));
		exit(2);
	}
	syslog(LOG_INFO, "Connected to MySQL!");
	return 0;
}

int check_sql_connection (){
	int alive;
	alive = mysql_ping(sock);
	if(alive != 0){
        	mysql_close(sock);
		if (!(sock = mysql_real_connect(&mysql,mysqmail_config.mysql_hostname,
			mysqmail_config.mysql_user,mysqmail_config.mysql_pass,mysqmail_config.mysql_db,0,NULL,0))){
			syslog(LOG_NOTICE, "Couldn't connect to engine: %s",mysql_error(&mysql));
			return 1;
		}else{
			syslog(LOG_NOTICE, "Reconnection to MySQL server with success!");
			return 0;
		}
	}
	return 0;
}

void my_cleanups(){
	sigset_t oldset;
	sigset_t newset;
	// Make it so a SIGQUIT / SIGTERM also kills the tail that we are piping.
	// Before calling killpg() we block SIGTERM to not receive it a 2nd time
	// and enter a signal loop.
	sigemptyset( &newset );
	sigaddset( &newset, SIGTERM );
	sigprocmask(SIG_BLOCK, &newset, &oldset);
	killpg(0,SIGTERM);
	sigprocmask(SIG_SETMASK, &oldset, NULL);
	// Close the MySQL connection
	mysql_close(sock);
	// Unlink the /var/run/DAEMON-NAME.pid
	unlink(pid_fn);
}

// Signal handler (HUP, TERM, etc.)
void sighand(int sig) {
	switch(sig) {
	case SIGHUP:
		break;
	case SIGQUIT:
	case SIGTERM:
        	my_cleanups();
		exit(0);
		break;
	default:
		break;
	}
}

void daemonize(){
	pid_t pid;
	pid = fork();
	// Fork failed
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}
	// We're parent, exit
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}
	// We're the child, get our pid
	if ((pid = getpid()) < 0) {
		exit(EXIT_FAILURE);
	}
	// Forwards the signals to the process we will open with popen, and be a session leader
	setsid();

	return;
}


void reg_hand(){
	signal(SIGHUP, sighand);
	signal(SIGTERM, sighand);
	signal(SIGINT, sighand);
	signal(SIGQUIT, sighand);
}


void write_pidfile(char* daemon_name){
	pid_t pid;
	FILE* fp;

	if ((pid = getpid()) < 0) {
		exit(EXIT_FAILURE);
	}

	strcpy(pid_fn,"/var/run/");
	strcat(pid_fn,daemon_name);
	strcat(pid_fn,".pid");
	fp = fopen(pid_fn,"w");
	if( fp == NULL ){
		syslog(LOG_NOTICE, "Couldn't open PID file %s: exiting.", pid_fn);
		my_cleanups();
	}else{
		fprintf(fp,"%d\n",pid);
		fclose(fp);
	}
}

// This read from fifo_path until EOF and reopen if the fifo
// is closed for whatever reason (eg: syslog restart)
int log_all_lines( char* file_path, int (*log_me)(char*) ){
	static char logline[MAX_LOG_LINE+1];
	FILE* fifo_p;
	char* tail_cmd;

	// Manages 2 style of syslog file location
	if(strcmp("syslog",file_path) == 0){
		if( mysqmail_config.syslog_file == NULL ){
			if( access("/var/log/syslog", R_OK) == 0){
				syslog(LOG_NOTICE, "Detected syslog in /var/log/syslog");
				tail_cmd = "tail -n 0 -F /var/log/syslog";
			}else if( access("/var/log/maillog", R_OK) == 0){
				syslog(LOG_NOTICE, "Detected syslog in /var/log/maillog");
				tail_cmd = "tail -n 0 -F /var/log/messages";
			}else if( access("/var/log/messages", R_OK) == 0){
				syslog(LOG_NOTICE, "Detected syslog in /var/log/messages");
				tail_cmd = "tail -n 0 -F /var/log/messages";
			}else{
				return 1;
			}
		}else{
			if( access(mysqmail_config.syslog_file, R_OK) == 0){
				syslog(LOG_NOTICE, "Using syslog in %s", mysqmail_config.syslog_file);
				tail_cmd = malloc (strlen("tail -n 0 -F ") + strlen(mysqmail_config.syslog_file) + 1);
				strcpy(tail_cmd,"tail -n 0 -F ");
				strcat(tail_cmd,mysqmail_config.syslog_file);
			}else{
				return 1;
			}
		}
	}else{
		if( access(file_path, R_OK) != 0){
			syslog(LOG_NOTICE, "Cannot access file_path");
			return 1;
		}

		syslog(LOG_NOTICE, "Using %s", file_path);
		tail_cmd = malloc( strlen("tail -n 0 -F ") + strlen(file_path) + 1);
		if( tail_cmd == NULL){
			return 1;
		}
		strcpy(tail_cmd,"tail -n 0 -F ");
		strcat(tail_cmd,file_path);
	}

	fifo_p = popen(tail_cmd,"r");
	if(fifo_p == NULL){
		syslog(LOG_NOTICE, "Couldn't open pipe to syslog: exiting.");
		free(tail_cmd);
		return 1;
	}
	while(NULL != fgets(logline,MAX_LOG_LINE-1,fifo_p)){
		check_sql_connection();
		log_me(logline);
	}
	syslog(LOG_NOTICE, "End of syslog pipe: exiting!");
	free(tail_cmd);
	my_cleanups();
	return 0;
}

