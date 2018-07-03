#ifndef _CONNECTIONS_H_
#define _CONNECTIONS_H_
#include "first.h"

#include "server.h"
#include "fdevent.h"


void connection_pool_init(server *srv);
void connections_free(server *srv);

connection * connection_accept(server *srv, server_socket *srv_sock);
connection * connection_accepted(server *srv, server_socket *srv_socket, sock_addr *cnt_addr, int cnt);

typedef handler_t (*con_fdevent_handler)(server *srv, connection *con, void *context, int revents);

ConEventHandler connection_fdevent_add(server *srv, connection *con, int fd, con_fdevent_handler handler, void *ctx, int events);
void connection_fdevent_set(ConEventHandler ev, int events);
void connection_fdevent_clr(ConEventHandler ev, int event);
void connection_fdevent_event_del(ConEventHandler ev);
int connection_fdevent_get_interest(ConEventHandler ev);
void connection_fdevent_sched_close(ConEventHandler ev);
void connection_joblist_append(connection *con);

int connection_set_state(server *srv, connection *con, connection_state_t state);
const char * connection_get_state(connection_state_t state);
const char * connection_get_short_state(connection_state_t state);
int connection_state_machine(server *srv, connection *con);

handler_t connection_handle_read_post_state(server *srv, connection *con);
handler_t connection_handle_read_post_error(server *srv, connection *con, int http_status);
int connection_write_chunkqueue(server *srv, connection *con, chunkqueue *c, off_t max_bytes);
void connection_response_reset(server *srv, connection *con);

#define FOR_ALL_CON(srv,con) for(con=mpool_get_next_usedmem(srv->connspool, NULL);con!=NULL; con=mpool_get_next_usedmem(srv->connspool, con) )
#endif
