/////////////////////////////////////////////////
// Config file reader stuff: using dotconf lib //
/////////////////////////////////////////////////
typedef struct{
	char* mysql_hostname;
	char* mysql_user;
	char* mysql_pass;
	char* mysql_db;
	char* mysql_table_smtp_logs;
	char* mysql_table_pop_access;
	char* mysql_table_scoreboard;
	char* mysql_table_domain;
	char* syslog_file;
}mysqmail_config_t;

int read_config_file();
