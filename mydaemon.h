#define MAX_LOG_LINE 1024

void sighand(int sig);
void daemonize();
void reg_hand();
void write_pidfile(char* daemon_name);
int log_all_lines( char* fifo_path, int (*log_me)(char*) );
int do_ze_mysql_connect();
