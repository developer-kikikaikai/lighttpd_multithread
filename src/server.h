#ifndef _SERVER_H_
#define _SERVER_H_
#include "first.h"

#include "base.h"

int config_read(server *srv, const char *fn);
int config_set_defaults(server *srv);

void server_set_cur_fds(int fd);
int  server_get_cur_fds(void);
void server_increment_cur_fds(void);
void server_decrement_cur_fds(void);

void server_update_ts_date_str(buffer *b);
void server_update_cur_ts(time_t cur_ts);
time_t server_get_cur_ts(void);
void server_init_ts(server *srv);
void server_exit_ts(void);
#endif
