#include "first.h"

#include "base.h"
#include "joblist.h"
#include "connections.h"
#include "server.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static connection *fdwaitqueue_unshift(server *srv, connections *fdwaitqueue);

static pthread_mutex_t joblist_lock=PTHREAD_MUTEX_INITIALIZER;
#define JOB_LOCK pthread_mutex_lock(&joblist_lock);
#define JOB_UNLOCK pthread_mutex_unlock(&joblist_lock);

int joblist_append(server *srv, connection *con) {
JOB_LOCK
	if (srv->joblist->size == 0) {
		srv->joblist->size  = 16;
		srv->joblist->ptr   = malloc(sizeof(*srv->joblist->ptr) * srv->joblist->size);
		force_assert(NULL != srv->joblist->ptr);
	} else if (srv->joblist->used == srv->joblist->size) {
		srv->joblist->size += 16;
		srv->joblist->ptr   = realloc(srv->joblist->ptr, sizeof(*srv->joblist->ptr) * srv->joblist->size);
		force_assert(NULL != srv->joblist->ptr);
	}

	srv->joblist->ptr[srv->joblist->used++] = con;
JOB_UNLOCK
	return 0;
}

void connection_joblist_append(connection *con) {
	con->is_appendjob=1;
}

void joblist_call(server *srv) {
JOB_LOCK
	size_t ndx;
	for (ndx = 0; ndx < srv->joblist->used; ndx++) {
		connection *con = srv->joblist->ptr[ndx];
		connection_state_machine(srv, con);
	}
	srv->joblist->used = 0;
JOB_UNLOCK
}

void joblist_free(server *srv, connections *joblist) {
	UNUSED(srv);

	free(joblist->ptr);
	free(joblist);
}

static connection *fdwaitqueue_unshift(server *srv, connections *fdwaitqueue) {
JOB_LOCK
	connection *con=NULL;
	UNUSED(srv);


	if (fdwaitqueue->used == 0) goto end;

	con = fdwaitqueue->ptr[0];

	memmove(fdwaitqueue->ptr, &(fdwaitqueue->ptr[1]), --fdwaitqueue->used * sizeof(*(fdwaitqueue->ptr)));

end:
JOB_UNLOCK
	return con;
}

int fdwaitqueue_append(server *srv, connection *con) {
JOB_LOCK
	if (srv->fdwaitqueue->size == 0) {
		srv->fdwaitqueue->size  = 16;
		srv->fdwaitqueue->ptr   = malloc(sizeof(*(srv->fdwaitqueue->ptr)) * srv->fdwaitqueue->size);
		force_assert(NULL != srv->fdwaitqueue->ptr);
	} else if (srv->fdwaitqueue->used == srv->fdwaitqueue->size) {
		srv->fdwaitqueue->size += 16;
		srv->fdwaitqueue->ptr   = realloc(srv->fdwaitqueue->ptr, sizeof(*(srv->fdwaitqueue->ptr)) * srv->fdwaitqueue->size);
		force_assert(NULL != srv->fdwaitqueue->ptr);
	}

	srv->fdwaitqueue->ptr[srv->fdwaitqueue->used++] = con;
JOB_UNLOCK
	return 0;
}

void fdwaitqueue_call(server *srv) {
JOB_LOCK
	/* we still have some fds to share */
	if (srv->want_fds) {
		/* check the fdwaitqueue for waiting fds */
		int free_fds = srv->max_fds - server_get_cur_fds() - 16;
		connection *con;

		for (; free_fds > 0 && NULL != (con = fdwaitqueue_unshift(srv, srv->fdwaitqueue)); free_fds--) {
			connection_state_machine(srv, con);

			srv->want_fds--;
		}
	}
JOB_UNLOCK
}

void fdwaitqueue_free(server *srv, connections *fdwaitqueue) {
	UNUSED(srv);
	free(fdwaitqueue->ptr);
	free(fdwaitqueue);
}
