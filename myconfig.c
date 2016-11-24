#include <stdlib.h>
#include <dotconf.h>
#include <string.h>
#include <syslog.h>

#include "myconfig.h"


extern mysqmail_config_t mysqmail_config;

DOTCONF_CB(cb_mysql_hostname);
DOTCONF_CB(cb_mysql_user);
DOTCONF_CB(cb_mysql_pass);
DOTCONF_CB(cb_mysql_db);
DOTCONF_CB(cb_mysql_table_smtp_logs);
DOTCONF_CB(cb_mysql_table_pop_access);
DOTCONF_CB(cb_mysql_table_scoreboard);
DOTCONF_CB(cb_mysql_table_domain);
DOTCONF_CB(cb_syslogfile_to_read);

static configoption_t options[] = {
	{"mysql_hostname", ARG_STR, cb_mysql_hostname, NULL, 0},
	{"mysql_user", ARG_STR, cb_mysql_user, NULL, 0},
	{"mysql_pass", ARG_STR, cb_mysql_pass, NULL, 0},
	{"mysql_db", ARG_STR, cb_mysql_db, NULL, 0},
	{"mysql_table_smtp_logs", ARG_STR, cb_mysql_table_smtp_logs, NULL, 0},
	{"mysql_table_pop_access", ARG_STR, cb_mysql_table_pop_access, NULL, 0},
	{"mysql_table_scoreboard", ARG_STR, cb_mysql_table_scoreboard, NULL, 0},
	{"mysql_table_domain", ARG_STR, cb_mysql_table_domain, NULL, 0},
	{"syslog_file", ARG_STR, cb_syslogfile_to_read, NULL, 0},
	LAST_OPTION
};

DOTCONF_CB(cb_mysql_hostname){
	mysqmail_config.mysql_hostname = (char*)malloc(strlen(cmd->data.str)+1);
	strcpy(mysqmail_config.mysql_hostname,cmd->data.str);
	return NULL;
}
DOTCONF_CB(cb_mysql_user){
	mysqmail_config.mysql_user = (char*)malloc(strlen(cmd->data.str)+1);
	strcpy(mysqmail_config.mysql_user,cmd->data.str);
	return NULL;
}
DOTCONF_CB(cb_mysql_pass){
	mysqmail_config.mysql_pass = (char*)malloc(strlen(cmd->data.str)+1);
	strcpy(mysqmail_config.mysql_pass,cmd->data.str);
	return NULL;
}
DOTCONF_CB(cb_mysql_db){
	mysqmail_config.mysql_db = (char*)malloc(strlen(cmd->data.str)+1);
	strcpy(mysqmail_config.mysql_db,cmd->data.str);
	return NULL;
}
DOTCONF_CB(cb_mysql_table_smtp_logs){
	mysqmail_config.mysql_table_smtp_logs = (char*)malloc(strlen(cmd->data.str)+1);
	strcpy(mysqmail_config.mysql_table_smtp_logs,cmd->data.str);
	return NULL;
}
DOTCONF_CB(cb_mysql_table_pop_access){
	mysqmail_config.mysql_table_pop_access = (char*)malloc(strlen(cmd->data.str)+1);
	strcpy(mysqmail_config.mysql_table_pop_access,cmd->data.str);
	return NULL;
}
DOTCONF_CB(cb_mysql_table_scoreboard){
	mysqmail_config.mysql_table_scoreboard = (char*)malloc(strlen(cmd->data.str)+1);
	strcpy(mysqmail_config.mysql_table_scoreboard,cmd->data.str);
	return NULL;
}
DOTCONF_CB(cb_mysql_table_domain){
	mysqmail_config.mysql_table_domain = (char*)malloc(strlen(cmd->data.str)+1);
	strcpy(mysqmail_config.mysql_table_domain,cmd->data.str);
	return NULL;
}
DOTCONF_CB(cb_syslogfile_to_read){
	mysqmail_config.syslog_file = (char*)malloc(strlen(cmd->data.str)+1);
	strcpy(mysqmail_config.syslog_file,cmd->data.str);
	return NULL;
}
int read_config_file(){
	configfile_t *configfile;
	configfile = dotconf_create("/etc/mysqmail.conf", options, 0, CASE_INSENSITIVE);
	if (dotconf_command_loop(configfile) == 0){
		syslog(LOG_ERR, "Error reading config file: exiting!");
		exit(2);
	}
	dotconf_cleanup(configfile);
	return 0;
}
