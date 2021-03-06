#ifndef _JOB_LIST_H_
#define _JOB_LIST_H_
#include "first.h"

#include "base.h"

int joblist_append(server *srv, connection *con);
void joblist_call(server *srv);
void joblist_free(server *srv, connections *joblist);

int fdwaitqueue_append(server *srv, connection *con);
void fdwaitqueue_call(server *srv);
void fdwaitqueue_free(server *srv, connections *fdwaitqueue);

#endif
