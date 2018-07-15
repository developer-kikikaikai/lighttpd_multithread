#include "first.h"

#include "buffer.h"
#include "server.h"
#include "log.h"
#include "connections.h"
#include "fdevent.h"

#include "configfile.h"
#include "request.h"
#include "response.h"
#include "network.h"
#include "http_chunk.h"
#include "stat_cache.h"
#include "joblist.h"

#include "plugin.h"

#include "inet_ntop_cache.h"
#include "state_machine.h"

#include <sys/stat.h>

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#ifdef HAVE_SYS_FILIO_H
# include <sys/filio.h>
#endif

#include "sys-socket.h"

typedef struct {
	        PLUGIN_DATA;
} plugin_data;

typedef struct connection_request_ctx_t {
	server *srv;
	connection * con;
	void *arg;
	con_fdevent_handler handler;
} connection_request_ctx_t;

/*event handler regi*/
struct connection_event_handler_t {
	/*to add event*/
	EventSubscriber subscriber;
	connection_request_ctx_t ctx;
	EventTPoolThreadInfo eventdata;
};

static inline int connection_tpoolevent2fdevent(int eventflag) {
	int ret_eveflag=0;
	if(eventflag&EV_TPOOL_READ) ret_eveflag |= FDEVENT_IN;
	if(eventflag&EV_TPOOL_WRITE) ret_eveflag |= FDEVENT_OUT;
	if(eventflag&EV_TPOOL_HUNGUP) ret_eveflag |= FDEVENT_HUP;
	return ret_eveflag;
}

static inline int connection_fdevent2tpoolevent(int eventflag) {
	int ret_eveflag=0;
	if(eventflag&FDEVENT_IN) ret_eveflag |= EV_TPOOL_READ;
	if(eventflag&FDEVENT_OUT) ret_eveflag |= EV_TPOOL_WRITE;
	if(eventflag&FDEVENT_HUP) ret_eveflag |= EV_TPOOL_HUNGUP;
	return ret_eveflag;
}

#define CON_MAX_EVENTFD (32*100)
#define CON_EVENTFD_SIZE (sizeof(connection_event_handler_t)+ sizeof(event_subscriber_t))
static int connection_handle_close_state(server *srv, connection *con);
static int connection_handle_response_end_state(server *srv, connection *con);
static int connection_handle_request_start_state(server *srv, connection *con);
static int connection_handle_request_end_state(server *srv, connection *con);
static int connection_handle_handle_request_state(server *srv, connection *con);
static int connection_handle_response_start_state(server *srv, connection *con);
static int connection_handle_connect_state(server *srv, connection *con);
static int connection_handle_write_state(server *srv, connection *con);
static int connection_handle_read_state(server *srv, connection *con);
static void connection_handle_fdevent_set(connection *con);
static void connection_state_machine_init(server *srv, connection *con);
static void connection_state_machine_exit(server *srv, connection *con);
static int connection_del(server *srv, connection *con);
static void connection_handle_fdevent_cb(int socketfd, int eventflag, void * event_arg);

static void connection_event_init(void *handle, void *param);
static void connection_init(server *srv, connection *con);
static int connection_reset(server *srv, connection *con);
static inline int connection_is_init(connection *con);
static void connection_pool_connection_init(void *this, void *srv);
//static void connection_pool_connection_init(connection *con, server *srv);

static inline int connection_get_ostate(connection *con) {
	return (con->state==CON_STATE_READ_POST)?CON_STATE_HANDLE_REQUEST:con->state;
}

static int connection_handle_base(int (*handle)(server *srv, connection *con), void * arg) {
	http_connection_t * http_con = (http_connection_t *)arg;
	if (http_con->srv->srvconf.log_state_handling) {
		log_error_write(http_con->srv, __FILE__, __LINE__, "sds",
				"state at start",
				http_con->con->fd,
				connection_get_state(http_con->con->state));
	}

	//fprintf(stderr, "thread:%x, %s state %s\n", (unsigned int)pthread_self(), __func__, connection_get_state(http_con->con->state));
	if( handle(http_con->srv, http_con->con) == -1 || http_con->ostate != (int)http_con->con->state ) {
		http_con->ostate = http_con->con->state;
		return state_machine_call_event(http_con->con->state_machine, CON_EVENT_RUN, arg, 0, NULL);
	}

	if (http_con->srv->srvconf.log_state_handling) {
		log_error_write(http_con->srv, __FILE__, __LINE__, "sds",
				"state at exit:",
				http_con->con->fd,
				connection_get_state(http_con->con->state));
	}

	connection_handle_fdevent_set(http_con->con);
	return 0;
}

static int connection_request_start_state(void *arg) {
	return connection_handle_base(connection_handle_request_start_state, arg);
}
static int connection_request_end_state(void *arg) {
	return connection_handle_base(connection_handle_request_end_state, arg);
}
static int connection_handle_request_state(void *arg) {
	return connection_handle_base(connection_handle_handle_request_state, arg);
}
static int connection_response_start_state(void *arg) {
	return connection_handle_base(connection_handle_response_start_state, arg);
}
static int connection_response_end_state(void *arg) {
	return connection_handle_base(connection_handle_response_end_state, arg);
}
static int connection_connect_state(void *arg) {
	return connection_handle_base(connection_handle_connect_state, arg);
}
static int connection_close_state(void *arg) {
	return connection_handle_base(connection_handle_close_state, arg);
}
static int connection_read_state(void *arg) {
	return connection_handle_base(connection_handle_read_state, arg);
}
static int connection_write_state(void *arg) {
	return connection_handle_base(connection_handle_write_state, arg);
}

#undef STATE_METHOD_DEFINE

static state_info_t state_info[] = {
	STATE_MNG_SET_INFO_INIT( CON_STATE_REQUEST_START,  connection_request_start_state),
	STATE_MNG_SET_INFO_INIT( CON_STATE_REQUEST_END,    connection_request_end_state),
	STATE_MNG_SET_INFO_INIT( CON_STATE_READ_POST,      connection_handle_request_state),
	STATE_MNG_SET_INFO_INIT( CON_STATE_HANDLE_REQUEST, connection_handle_request_state),
	STATE_MNG_SET_INFO_INIT( CON_STATE_RESPONSE_START, connection_response_start_state),
	STATE_MNG_SET_INFO_INIT( CON_STATE_RESPONSE_END,   connection_response_end_state),
	STATE_MNG_SET_INFO_INIT( CON_STATE_ERROR,          connection_response_end_state),
	STATE_MNG_SET_INFO_INIT( CON_STATE_CONNECT,        connection_connect_state),
	STATE_MNG_SET_INFO_INIT( CON_STATE_CLOSE,          connection_close_state),
	STATE_MNG_SET_INFO_INIT( CON_STATE_READ,           connection_read_state),
	STATE_MNG_SET_INFO_INIT( CON_STATE_WRITE,          connection_write_state),
};

//Is it OK to use same state info table?
static const state_event_info_t state_event[] = {
	{CON_EVENT_RUN, sizeof(state_info)/sizeof(state_info[0]), state_info},
};

static connection *connections_get_new_connection(server *srv) {
	//fprintf(stderr, "%s\n", __func__);

	connection * con = mpool_get(srv->connspool);
	force_assert(con != NULL);

	if(!connection_is_init(con)) {
		connection_reset(srv, con);
	}

	return con;
}

static int connection_del(server *srv, connection *con) {
	if (con == NULL) return -1;

	buffer_reset(con->uri.authority);
	buffer_reset(con->uri.path);
	buffer_reset(con->uri.query);
	buffer_reset(con->request.orig_uri);

	/* not last element */
	mpool_release(srv->connspool, con);

	return 0;
}

static int connection_close(server *srv, connection *con) {
	int fd = con->fd;
	if (con->fd < 0)con->fd = -con->fd;
//	fprintf(stderr, "[%x]%s\n",(unsigned int )pthread_self(),  __func__);

	plugins_call_handle_connection_close(srv, con);

	if(0 < fd) {
	connection_fdevent_event_del(con->client_handler);
#ifdef __WIN32
	if (closesocket(con->fd)) {
		log_error_write(srv, __FILE__, __LINE__, "sds",
				"(warning) close:", con->fd, strerror(errno));
	}
#else
	if (close(con->fd)) {
		log_error_write(srv, __FILE__, __LINE__, "sds",
				"(warning) close:", con->fd, strerror(errno));
	}
#endif
	else {
		server_decrement_cur_fds();
	}
//	fprintf(stderr, "[%x]%s closed\n",(unsigned int )pthread_self(),  __func__);

	if (srv->srvconf.log_state_handling) {
		log_error_write(srv, __FILE__, __LINE__, "sd",
				"connection closed for fd", con->fd);
	}
	}
//	fprintf(stderr, "%s %d\n", __func__, con->fd);
	con->fd = -1;

	/* plugins should have cleaned themselves up */
	for (size_t i = 0; i < srv->plugins.used; ++i) {
		plugin *p = ((plugin **)(srv->plugins.ptr))[i];
		plugin_data *pd = p->data;
		if (!pd || NULL == con->plugin_ctx[pd->id]) continue;
		log_error_write(srv, __FILE__, __LINE__, "sb",
				"missing cleanup in", p->name);
		con->plugin_ctx[pd->id] = NULL;
	}

	connection_del(srv, con);
	connection_set_state(srv, con, CON_STATE_CONNECT);
	//add joblist to run state_machine
	return 0;
}

static void connection_read_for_eos(server *srv, connection *con) {
	/* we have to do the linger_on_close stuff regardless
	 * of con->keep_alive; even non-keepalive sockets may
	 * still have unread data, and closing before reading
	 * it will make the client not see all our output.
	 */
	UNUSED(srv);
	ssize_t len;
	const int type = con->dst_addr.plain.sa_family;
	char buf[16384];
	do {
		len = fdevent_socket_read_discard(con->fd, buf, sizeof(buf),
						  type, SOCK_STREAM);
	} while (len > 0 || (len < 0 && errno == EINTR));

	if (len < 0 && errno == EAGAIN) return;
      #if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
	if (len < 0 && errno == EWOULDBLOCK) return;
      #endif

	/* 0 == len || (len < 0 && (errno is a non-recoverable error)) */
		con->close_timeout_ts = server_get_cur_ts() - (HTTP_LINGER_TIMEOUT+1);
}

static int connection_handle_close_state(server *srv, connection *con) {
	connection_read_for_eos(srv, con);

	if (server_get_cur_ts() - con->close_timeout_ts > HTTP_LINGER_TIMEOUT) {
		connection_close(srv, con);
	}
	return 0;
}

static void connection_handle_shutdown(server *srv, connection *con) {
//	fprintf(stderr, "%s shutdown\n", __func__);
	plugins_call_handle_connection_shut_wr(srv, con);

	connection_reset(srv, con);

	/* close the connection */
	if (con->fd >= 0 && 0 == shutdown(con->fd, SHUT_WR)) {
		con->close_timeout_ts = server_get_cur_ts();
		connection_set_state(srv, con, CON_STATE_CLOSE);

		if (srv->srvconf.log_state_handling) {
			log_error_write(srv, __FILE__, __LINE__, "sd",
					"shutdown for fd", con->fd);
		}
	} else {
		connection_close(srv, con);
	}
}

static int connection_handle_response_end_state(server *srv, connection *con) {
        /* log the request */
        /* (even if error, connection dropped, still write to access log if http_status) */
	if (con->http_status) {
		plugins_call_handle_request_done(srv, con);
	}

	if (con->request.content_length != con->request_content_queue->bytes_in
	    || con->state == CON_STATE_ERROR) {
		/* request body is present and has not been read completely */
		con->keep_alive = 0;
	}

        if (con->keep_alive) {
		connection_reset(srv, con);
		connection_set_state(srv, con, CON_STATE_REQUEST_START);
	} else {
		connection_handle_shutdown(srv, con);
	}
	return 0;
}

static void connection_handle_errdoc_init(server *srv, connection *con) {
	/* modules that produce headers required with error response should
	 * typically also produce an error document.  Make an exception for
	 * mod_auth WWW-Authenticate response header. */
	buffer *www_auth = NULL;
	if (401 == con->http_status) {
		data_string *ds = (data_string *)array_get_element(con->response.headers, "WWW-Authenticate");
		if (NULL != ds) {
			www_auth = buffer_init_buffer(ds->value);
		}
	}

	con->response.transfer_encoding = 0;
	buffer_reset(con->physical.path);
	array_reset(con->response.headers);
	chunkqueue_reset(con->write_queue);

	if (NULL != www_auth) {
		response_header_insert(srv, con, CONST_STR_LEN("WWW-Authenticate"), CONST_BUF_LEN(www_auth));
		buffer_free(www_auth);
	}
}

static int connection_handle_write_prepare(server *srv, connection *con) {
	if (con->mode == DIRECT) {
		/* static files */
		switch(con->request.http_method) {
		case HTTP_METHOD_GET:
		case HTTP_METHOD_POST:
		case HTTP_METHOD_HEAD:
			break;
		case HTTP_METHOD_OPTIONS:
			/*
			 * 400 is coming from the request-parser BEFORE uri.path is set
			 * 403 is from the response handler when noone else catched it
			 *
			 * */
			if ((!con->http_status || con->http_status == 200) && !buffer_string_is_empty(con->uri.path) &&
			    con->uri.path->ptr[0] != '*') {
				response_header_insert(srv, con, CONST_STR_LEN("Allow"), CONST_STR_LEN("OPTIONS, GET, HEAD, POST"));

				con->response.transfer_encoding &= ~HTTP_TRANSFER_ENCODING_CHUNKED;
				con->parsed_response &= ~HTTP_CONTENT_LENGTH;

				con->http_status = 200;
				con->file_finished = 1;

				chunkqueue_reset(con->write_queue);
			}
			break;
		default:
			if (0 == con->http_status) {
				con->http_status = 501;
			}
			break;
		}
	}

	if (con->http_status == 0) {
		con->http_status = 403;
	}

	switch(con->http_status) {
	case 204: /* class: header only */
	case 205:
	case 304:
		/* disable chunked encoding again as we have no body */
		con->response.transfer_encoding &= ~HTTP_TRANSFER_ENCODING_CHUNKED;
		con->parsed_response &= ~HTTP_CONTENT_LENGTH;
		chunkqueue_reset(con->write_queue);

		con->file_finished = 1;
		break;
	default: /* class: header + body */
		/* only custom body for 4xx and 5xx */
		if (con->http_status < 400 || con->http_status >= 600) break;

		if (con->mode != DIRECT && (!con->conf.error_intercept || con->error_handler_saved_status)) break;

		con->file_finished = 0;

		connection_handle_errdoc_init(srv, con);

		/* try to send static errorfile */
		if (!buffer_string_is_empty(con->conf.errorfile_prefix)) {
			stat_cache_entry *sce = NULL;

			buffer_copy_buffer(con->physical.path, con->conf.errorfile_prefix);
			buffer_append_int(con->physical.path, con->http_status);
			buffer_append_string_len(con->physical.path, CONST_STR_LEN(".html"));

			if (0 == http_chunk_append_file(srv, con, con->physical.path)) {
				con->file_finished = 1;
				if (HANDLER_ERROR != stat_cache_get_entry(srv, con, con->physical.path, &sce)) {
					stat_cache_content_type_get(srv, con, con->physical.path, sce);
					response_header_overwrite(srv, con, CONST_STR_LEN("Content-Type"), CONST_BUF_LEN(sce->content_type));
				}
			}
		}

		if (!con->file_finished) {
			buffer *b;

			buffer_reset(con->physical.path);

			con->file_finished = 1;
			b = buffer_init();

			/* build default error-page */
			buffer_copy_string_len(b, CONST_STR_LEN(
					   "<?xml version=\"1.0\" encoding=\"iso-8859-1\"?>\n"
					   "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\n"
					   "         \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n"
					   "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n"
					   " <head>\n"
					   "  <title>"));
			buffer_append_int(b, con->http_status);
			buffer_append_string_len(b, CONST_STR_LEN(" - "));
			buffer_append_string(b, get_http_status_name(con->http_status));

			buffer_append_string_len(b, CONST_STR_LEN(
					     "</title>\n"
					     " </head>\n"
					     " <body>\n"
					     "  <h1>"));
			buffer_append_int(b, con->http_status);
			buffer_append_string_len(b, CONST_STR_LEN(" - "));
			buffer_append_string(b, get_http_status_name(con->http_status));

			buffer_append_string_len(b, CONST_STR_LEN("</h1>\n"
					     " </body>\n"
					     "</html>\n"
					     ));

			(void)http_chunk_append_buffer(srv, con, b);
			buffer_free(b);

			response_header_overwrite(srv, con, CONST_STR_LEN("Content-Type"), CONST_STR_LEN("text/html"));
		}
		break;
	}

	/* Allow filter plugins to change response headers before they are written. */
	switch(plugins_call_handle_response_start(srv, con)) {
	case HANDLER_GO_ON:
	case HANDLER_FINISHED:
		break;
	default:
		log_error_write(srv, __FILE__, __LINE__, "s", "response_start plugin failed");
		return -1;
	}

	if (con->file_finished) {
		/* we have all the content and chunked encoding is not used, set a content-length */

		if (!(con->parsed_response & (HTTP_CONTENT_LENGTH|HTTP_TRANSFER_ENCODING))) {
			off_t qlen = chunkqueue_length(con->write_queue);

			/**
			 * The Content-Length header only can be sent if we have content:
			 * - HEAD doesn't have a content-body (but have a content-length)
			 * - 1xx, 204 and 304 don't have a content-body (RFC 2616 Section 4.3)
			 *
			 * Otherwise generate a Content-Length header as chunked encoding is not 
			 * available
			 */
			if ((con->http_status >= 100 && con->http_status < 200) ||
			    con->http_status == 204 ||
			    con->http_status == 304) {
				data_string *ds;
				/* no Content-Body, no Content-Length */
				if (NULL != (ds = (data_string*) array_get_element(con->response.headers, "Content-Length"))) {
					buffer_reset(ds->value); /* Headers with empty values are ignored for output */
				}
			} else if (qlen > 0 || con->request.http_method != HTTP_METHOD_HEAD) {
				/* qlen = 0 is important for Redirects (301, ...) as they MAY have
				 * a content. Browsers are waiting for a Content otherwise
				 */
				buffer_copy_int(con->tmp_buf, qlen);

				response_header_overwrite(srv, con, CONST_STR_LEN("Content-Length"), CONST_BUF_LEN(con->tmp_buf));
			}
		}
	} else {
		/**
		 * the file isn't finished yet, but we have all headers
		 *
		 * to get keep-alive we either need:
		 * - Content-Length: ... (HTTP/1.0 and HTTP/1.0) or
		 * - Transfer-Encoding: chunked (HTTP/1.1)
		 * - Upgrade: ... (lighttpd then acts as transparent proxy)
		 */

		if (!(con->parsed_response & (HTTP_CONTENT_LENGTH|HTTP_TRANSFER_ENCODING|HTTP_UPGRADE))) {
			if (con->request.http_method == HTTP_METHOD_CONNECT
			    && con->http_status == 200) {
				/*(no transfer-encoding if successful CONNECT)*/
			} else if (con->request.http_version == HTTP_VERSION_1_1) {
				off_t qlen = chunkqueue_length(con->write_queue);
				con->response.transfer_encoding = HTTP_TRANSFER_ENCODING_CHUNKED;
				if (qlen) {
					/* create initial Transfer-Encoding: chunked segment */
					buffer *b = con->tmp_chunk_len;
					buffer_string_set_length(b, 0);
					buffer_append_uint_hex(b, (uintmax_t)qlen);
					buffer_append_string_len(b, CONST_STR_LEN("\r\n"));
					chunkqueue_prepend_buffer(con->write_queue, b);
					chunkqueue_append_mem(con->write_queue, CONST_STR_LEN("\r\n"));
				}
				response_header_append(srv, con, CONST_STR_LEN("Transfer-Encoding"), CONST_STR_LEN("chunked"));
			} else {
				con->keep_alive = 0;
			}
		}

		/**
		 * if the backend sent a Connection: close, follow the wish
		 *
		 * NOTE: if the backend sent Connection: Keep-Alive, but no Content-Length, we
		 * will close the connection. That's fine. We can always decide the close 
		 * the connection
		 *
		 * FIXME: to be nice we should remove the Connection: ... 
		 */
		if (con->parsed_response & HTTP_CONNECTION) {
			/* a subrequest disable keep-alive although the client wanted it */
			if (con->keep_alive && !con->response.keep_alive) {
				con->keep_alive = 0;
			}
		}
	}

	if (con->request.http_method == HTTP_METHOD_HEAD) {
		/**
		 * a HEAD request has the same as a GET 
		 * without the content
		 */
		con->file_finished = 1;

		chunkqueue_reset(con->write_queue);
		con->response.transfer_encoding &= ~HTTP_TRANSFER_ENCODING_CHUNKED;
		if (con->parsed_response & HTTP_TRANSFER_ENCODING) {
			data_string *ds;
			if (NULL != (ds = (data_string*) array_get_element(con->response.headers, "Transfer-Encoding"))) {
				buffer_reset(ds->value); /* Headers with empty values are ignored for output */
			}
		}
	}

	http_response_write_header(srv, con);

	return 0;
}

static int connection_handle_write(server *srv, connection *con) {
	switch(connection_write_chunkqueue(srv, con, con->write_queue, MAX_WRITE_LIMIT)) {
	case 0:
		con->write_request_ts = server_get_cur_ts();
		if (con->file_finished) {
			connection_set_state(srv, con, CON_STATE_RESPONSE_END);
		}
		break;
	case -1: /* error on our side */
		log_error_write(srv, __FILE__, __LINE__, "sd",
				"connection closed: write failed on fd", con->fd);
		connection_set_state(srv, con, CON_STATE_ERROR);
		break;
	case -2: /* remote close */
		connection_set_state(srv, con, CON_STATE_ERROR);
		break;
	case 1:
		con->write_request_ts = server_get_cur_ts();
		con->is_writable = 0;

		/* not finished yet -> WRITE */
		break;
	}

	return 0;
}

static void connection_event_init(void *handle, void *param) {
	UNUSED(param);
	ConEventHandler instance = (ConEventHandler)handle;
	memset(instance, 0, CON_EVENTFD_SIZE);
	instance->subscriber = (EventSubscriber)(instance + 1);
	/*event callback is fixed, this is wrapper of con_fdevent_handler*/
	instance->subscriber->event_callback = connection_handle_fdevent_cb;
}

static void connection_init(server *srv, connection *con) {
	con->fd = 0;
	con->fde_ndx = -1;
	con->bytes_written = 0;
	con->bytes_read = 0;
	con->bytes_header = 0;
	con->loops_per_request = 0;

#define CLEAN(x) \
	con->x = buffer_init();

	CLEAN(request.uri);
	CLEAN(request.request_line);
	CLEAN(request.request);
	CLEAN(request.pathinfo);

	CLEAN(request.orig_uri);

	CLEAN(uri.scheme);
	CLEAN(uri.authority);
	CLEAN(uri.path);
	CLEAN(uri.path_raw);
	CLEAN(uri.query);

	CLEAN(physical.doc_root);
	CLEAN(physical.path);
	CLEAN(physical.basedir);
	CLEAN(physical.rel_path);
	CLEAN(physical.etag);
	CLEAN(parse_request);

	CLEAN(server_name);
	CLEAN(proto);
	CLEAN(dst_addr_buf);

	CLEAN(tmp_buf);
	CLEAN(tmp_chunk_len);
#undef CLEAN
	con->write_queue = chunkqueue_init();
	con->read_queue = chunkqueue_init();
	con->request_content_queue = chunkqueue_init();

	con->request.headers      = array_init();
	con->response.headers     = array_init();
	con->environment     = array_init();
	con->split_vals = array_init();

	/* init plugin specific connection structures */

	con->plugin_ctx = calloc(1, (srv->plugins.used + 1) * sizeof(void *));
	force_assert(NULL != con->plugin_ctx);

	con->cond_cache = calloc(srv->config_context->used, sizeof(cond_cache_t));
	force_assert(NULL != con->cond_cache);
	config_setup_connection(srv, con);

	connection_state_machine_init(srv, con);
	con->event_pool = mpool_create(CON_EVENTFD_SIZE, CON_MAX_EVENTFD, 1, connection_event_init, NULL);
	force_assert(con->event_pool);
	con->is_appendjob = 0;
}

static inline int connection_is_init(connection * con) {
	return buffer_string_is_empty(con->request.uri);
}

static void connection_pool_connection_init(void *this, void *param) {
	connection *con = (connection *)this;
	server *srv = (server *)param;

	connection_init(srv, con);
}


void connection_pool_init(server *srv) {
	srv->connspool = mpool_create(sizeof(connection), srv->max_conns, 1, connection_pool_connection_init, srv);
	force_assert(NULL != srv->connspool);
}

void connections_free(server *srv) {
	connection *con;

	FOR_ALL_CON(srv, con) {
		connection_state_machine_exit(srv, con);
		mpool_delete(con->event_pool, NULL);

		connection_reset(srv, con);

		chunkqueue_free(con->write_queue);
		chunkqueue_free(con->read_queue);
		chunkqueue_free(con->request_content_queue);
		array_free(con->request.headers);
		array_free(con->response.headers);
		array_free(con->environment);

#define CLEAN(x) \
	buffer_free(con->x);

		CLEAN(request.uri);
		CLEAN(request.request_line);
		CLEAN(request.request);
		CLEAN(request.pathinfo);

		CLEAN(request.orig_uri);

		CLEAN(uri.scheme);
		CLEAN(uri.authority);
		CLEAN(uri.path);
		CLEAN(uri.path_raw);
		CLEAN(uri.query);

		CLEAN(physical.doc_root);
		CLEAN(physical.path);
		CLEAN(physical.basedir);
		CLEAN(physical.etag);
		CLEAN(physical.rel_path);
		CLEAN(parse_request);

		CLEAN(server_name);
		CLEAN(proto);
		CLEAN(dst_addr_buf);

		CLEAN(tmp_buf);
		CLEAN(tmp_chunk_len);
#undef CLEAN
		free(con->plugin_ctx);
		free(con->cond_cache);
		array_free(con->split_vals);
	}

	mpool_delete(srv->connspool, NULL);
	srv->connspool = NULL;
}


static int connection_reset(server *srv, connection *con) {
//	fprintf(stderr, "[%x]%s\n",(unsigned int )pthread_self(),  __func__);
	plugins_call_connection_reset(srv, con);

	connection_response_reset(srv, con);
	con->is_readable = 1;

	con->bytes_written = 0;
	con->bytes_written_cur_second = 0;
	con->bytes_read = 0;
	con->bytes_header = 0;
	con->loops_per_request = 0;

	con->request.http_method = HTTP_METHOD_UNSET;
	con->request.http_version = HTTP_VERSION_UNSET;

	con->request.http_if_modified_since = NULL;
	con->request.http_if_none_match = NULL;

#define CLEAN(x) \
	if (con->x) buffer_reset(con->x);

	CLEAN(request.uri);
	CLEAN(request.request_line);
	CLEAN(request.pathinfo);
	CLEAN(request.request);

	/* CLEAN(request.orig_uri); */

	CLEAN(uri.scheme);
	/* CLEAN(uri.authority); */
	/* CLEAN(uri.path); */
	CLEAN(uri.path_raw);
	/* CLEAN(uri.query); */

	CLEAN(parse_request);

	CLEAN(server_name);
	/*CLEAN(proto);*//* set to default in connection_accepted() */

	CLEAN(tmp_buf);
	CLEAN(tmp_chunk_len);
#undef CLEAN

#define CLEAN(x) \
	if (con->x) con->x->used = 0;

#undef CLEAN

#define CLEAN(x) \
		con->request.x = NULL;

	CLEAN(http_host);
	CLEAN(http_range);
	CLEAN(http_content_type);
#undef CLEAN
	con->request.content_length = 0;
	con->request.te_chunked = 0;

	array_reset(con->request.headers);
	array_reset(con->environment);
	array_reset(con->split_vals);

	chunkqueue_reset(con->request_content_queue);

	/* The cond_cache gets reset in response.c */
	/* config_cond_cache_reset(srv, con); */

	con->header_len = 0;
	con->async_callback = 0;
	con->error_handler_saved_status = 0;

	con->is_appendjob = 0;
	/*con->error_handler_saved_method = HTTP_METHOD_UNSET;*/
	/*(error_handler_saved_method value is not valid unless error_handler_saved_status is set)*/

	config_setup_connection(srv, con);
//	fprintf(stderr, "[%x]%s exit\n",(unsigned int )pthread_self(),  __func__);

	return 0;
}

static int connection_handle_request_start_state(server *srv, connection *con) {
//	fprintf(stderr, "request start, thread:%x, con=%p\n", (unsigned int)pthread_self(), (void *)con);
	con->request_start = server_get_cur_ts();
	con->read_idle_ts = con->request_start;
	if (con->conf.high_precision_timestamps)
		log_clock_gettime_realtime(&con->request_start_hp);

	con->request_count++;
	con->loops_per_request = 0;

	connection_set_state(srv, con, CON_STATE_READ);
	return 0;
}

static int connection_handle_request_end_state(server *srv, connection *con) {
	buffer_reset(con->uri.authority);
	buffer_reset(con->uri.path);
	buffer_reset(con->uri.query);
	buffer_reset(con->request.orig_uri);

	if (http_request_parse(srv, con)) {
		/* we have to read some data from the POST request */

		connection_set_state(srv, con, CON_STATE_READ_POST);

		return 0;
	}

	connection_set_state(srv, con, CON_STATE_HANDLE_REQUEST);
	return 0;
}

static int connection_handle_handle_request_state(server *srv, connection *con) {
	/*
	 * the request is parsed
	 *
	 * decided what to do with the request
	 * -
	 *
	 *
	 */
	int r = 0, done = 0;
	switch (r = http_response_prepare(srv, con)) {
	case HANDLER_WAIT_FOR_EVENT:
		if (!con->file_finished && (!con->file_started || 0 == con->conf.stream_response_body)) {
			break; /* come back here */
		}
		/* response headers received from backend; fall through to start response */
		/* fall through */
	case HANDLER_FINISHED:
		if (con->error_handler_saved_status > 0) {
			con->request.http_method = con->error_handler_saved_method;
		}
		if (con->mode == DIRECT || con->conf.error_intercept) {
			if (con->error_handler_saved_status) {
				if (con->error_handler_saved_status > 0) {
					con->http_status = con->error_handler_saved_status;
				} else if (con->http_status == 404 || con->http_status == 403) {
					/* error-handler-404 is a 404 */
					con->http_status = -con->error_handler_saved_status;
				} else {
					/* error-handler-404 is back and has generated content */
					/* if Status: was set, take it otherwise use 200 */
				}
			} else if (con->http_status >= 400) {
				buffer *error_handler = NULL;
				if (!buffer_string_is_empty(con->conf.error_handler)) {
					error_handler = con->conf.error_handler;
				} else if ((con->http_status == 404 || con->http_status == 403)
					   && !buffer_string_is_empty(con->conf.error_handler_404)) {
					error_handler = con->conf.error_handler_404;
				}

				if (error_handler) {
					/* call error-handler */

					/* set REDIRECT_STATUS to save current HTTP status code
					 * for access by dynamic handlers
					 * https://redmine.lighttpd.net/issues/1828 */
					data_string *ds;
					if (NULL == (ds = (data_string *)array_get_unused_element(con->environment, TYPE_STRING))) {
						ds = data_string_init();
					}
					buffer_copy_string_len(ds->key, CONST_STR_LEN("REDIRECT_STATUS"));
					buffer_append_int(ds->value, con->http_status);
					array_insert_unique(con->environment, (data_unset *)ds);

					if (error_handler == con->conf.error_handler) {
						plugins_call_connection_reset(srv, con);

						if (con->request.content_length) {
							if (con->request.content_length != con->request_content_queue->bytes_in) {
								con->keep_alive = 0;
							}
							con->request.content_length = 0;
							chunkqueue_reset(con->request_content_queue);
						}

						con->is_writable = 1;
						con->file_finished = 0;
						con->file_started = 0;
						con->parsed_response = 0;
						con->response.keep_alive = 0;
						con->response.content_length = -1;
						con->response.transfer_encoding = 0;

						con->error_handler_saved_status = con->http_status;
						con->error_handler_saved_method = con->request.http_method;

						con->request.http_method = HTTP_METHOD_GET;
					} else { /*(preserve behavior for server.error-handler-404)*/
						con->error_handler_saved_status = -con->http_status; /*(negative to flag old behavior)*/
					}

					buffer_copy_buffer(con->request.uri, error_handler);
					connection_handle_errdoc_init(srv, con);
					con->http_status = 0; /*(after connection_handle_errdoc_init())*/

					done = -1;
					break;
				}
			}
		}
		if (con->http_status == 0) con->http_status = 200;

		/* we have something to send, go on */
		connection_set_state(srv, con, CON_STATE_RESPONSE_START);
		break;
	case HANDLER_WAIT_FOR_FD:
		srv->want_fds++;

		fdwaitqueue_append(srv, con);

		break;
	case HANDLER_COMEBACK:
		done = -1;
		break;
	case HANDLER_ERROR:
		/* something went wrong */
		connection_set_state(srv, con, CON_STATE_ERROR);
		break;
	default:
		log_error_write(srv, __FILE__, __LINE__, "sdd", "unknown ret-value: ", con->fd, r);
		break;
	}
	return done;
}

static int connection_handle_response_start_state(server *srv, connection *con) {
	/*
	 * the decision is done
	 * - create the HTTP-Response-Header
	 *
	 */

	if (-1 == connection_handle_write_prepare(srv, con)) {
		connection_set_state(srv, con, CON_STATE_ERROR);

		return 0;
	}

	connection_set_state(srv, con, CON_STATE_WRITE);
	return 0;
}

static int connection_handle_connect_state(server *srv, connection *con) {
	UNUSED(srv);
	chunkqueue_reset(con->read_queue);
	con->request_count = 0;
	return 0;
}

static int connection_handle_write_state(server *srv, connection *con) {
	int r=0;
	do {
		/* only try to write if we have something in the queue */
		if (!chunkqueue_is_empty(con->write_queue)) {
			if (con->is_writable) {
				if (-1 == connection_handle_write(srv, con)) {
					log_error_write(srv, __FILE__, __LINE__, "ds",
							con->fd,
							"handle write failed.");
					connection_set_state(srv, con, CON_STATE_ERROR);
					break;
				}
				if (con->state != CON_STATE_WRITE) break;
			}
		} else if (con->file_finished) {
			connection_set_state(srv, con, CON_STATE_RESPONSE_END);
			break;
		}

		if (con->mode != DIRECT && !con->file_finished) {
			switch(r = plugins_call_handle_subrequest(srv, con)) {
			case HANDLER_WAIT_FOR_EVENT:
			case HANDLER_FINISHED:
			case HANDLER_GO_ON:
				break;
			case HANDLER_WAIT_FOR_FD:
				srv->want_fds++;
				fdwaitqueue_append(srv, con);
				break;
			case HANDLER_COMEBACK:
			default:
				log_error_write(srv, __FILE__, __LINE__, "sdd", "unexpected subrequest handler ret-value: ", con->fd, r);
				/* fall through */
			case HANDLER_ERROR:
				connection_set_state(srv, con, CON_STATE_ERROR);
				break;
			}
		}
	} while (con->state == CON_STATE_WRITE && (!chunkqueue_is_empty(con->write_queue) ? con->is_writable : con->file_finished));
	return 0;
}

/**
 * handle all header and content read
 *
 * we get called by the state-engine and by the fdevent-handler
 */
static int connection_handle_read_state(server *srv, connection *con)  {
	chunk *c, *last_chunk;
	off_t last_offset;
	chunkqueue *cq = con->read_queue;
	int is_closed = 0; /* the connection got closed, if we don't have a complete header, -> error */
	/* when in CON_STATE_READ: about to receive first byte for a request: */
	int is_request_start = chunkqueue_is_empty(cq);

	time_t cur_ts = server_get_cur_ts();

	if (con->is_readable) {
		con->read_idle_ts = cur_ts;

		switch(con->network_read(srv, con, con->read_queue, MAX_READ_LIMIT)) {
		case -1:
			connection_set_state(srv, con, CON_STATE_ERROR);
			return -1;
		case -2:
			is_closed = 1;
			break;
		default:
			break;
		}
	}

	chunkqueue_remove_finished_chunks(cq);

	/* we might have got several packets at once
	 */

	/* update request_start timestamp when first byte of
	 * next request is received on a keep-alive connection */
	if (con->request_count > 1 && is_request_start) {
		con->request_start = cur_ts;
		if (con->conf.high_precision_timestamps)
			log_clock_gettime_realtime(&con->request_start_hp);
	}

		/* if there is a \r\n\r\n in the chunkqueue
		 *
		 * scan the chunk-queue twice
		 * 1. to find the \r\n\r\n
		 * 2. to copy the header-packet
		 *
		 */

		last_chunk = NULL;
		last_offset = 0;

		for (c = cq->first; c; c = c->next) {
			size_t i;
			size_t len = buffer_string_length(c->mem) - c->offset;
			const char *b = c->mem->ptr + c->offset;

			for (i = 0; i < len; ++i) {
				char ch = b[i];

				if ('\r' == ch) {
					/* chec if \n\r\n follows */
					size_t j = i+1;
					chunk *cc = c;
					const char header_end[] = "\r\n\r\n";
					int header_end_match_pos = 1;

					for ( ; cc; cc = cc->next, j = 0 ) {
						size_t bblen = buffer_string_length(cc->mem) - cc->offset;
						const char *bb = cc->mem->ptr + cc->offset;

						for ( ; j < bblen; j++) {
							ch = bb[j];

							if (ch == header_end[header_end_match_pos]) {
								header_end_match_pos++;
								if (4 == header_end_match_pos) {
									last_chunk = cc;
									last_offset = j+1;
									goto found_header_end;
								}
							} else {
								goto reset_search;
							}
						}
					}
				} else if ('\n' == ch) {
					/* check if \n follows */
					if (i+1 < len) {
						if (b[i+1] == '\n') {
							last_chunk = c;
							last_offset = i+2;
							break;
						} /* else goto reset_search; */
					} else {
						for (chunk *cc = c->next; cc; cc = cc->next) {
							size_t bblen = buffer_string_length(cc->mem) - cc->offset;
							const char *bb = cc->mem->ptr + cc->offset;
							if (0 == bblen) continue;
							if (bb[0] == '\n') {
								last_chunk = cc;
								last_offset = 1;
								goto found_header_end;
							} else {
								goto reset_search;
							}
						}
					}
				}
reset_search: ;
			}
		}
found_header_end:

		/* found */
		if (last_chunk) {
			buffer_reset(con->request.request);

			for (c = cq->first; c; c = c->next) {
				size_t len = buffer_string_length(c->mem) - c->offset;

				if (c == last_chunk) {
					len = last_offset;
				}

				buffer_append_string_len(con->request.request, c->mem->ptr + c->offset, len);
				c->offset += len;
				cq->bytes_out += len;

				if (c == last_chunk) break;
			}

			connection_set_state(srv, con, CON_STATE_REQUEST_END);
		} else if (is_closed) {
			/* the connection got closed and we didn't got enough data to leave CON_STATE_READ;
			 * the only way is to leave here */
			connection_set_state(srv, con, CON_STATE_ERROR);
		}

		if ((last_chunk ? buffer_string_length(con->request.request) : (size_t)chunkqueue_length(cq))
		    > srv->srvconf.max_request_field_size) {
			log_error_write(srv, __FILE__, __LINE__, "s", "oversized request-header -> sending Status 431");
			con->http_status = 431; /* Request Header Fields Too Large */
			con->keep_alive = 0;
			connection_set_state(srv, con, CON_STATE_HANDLE_REQUEST);
		}

	chunkqueue_remove_finished_chunks(cq);

	return 0;
}

static void connection_handle_fdevent_set(connection *con) {
	int r = 0;
	switch(con->state) {
	case CON_STATE_READ:
		r = FDEVENT_IN | FDEVENT_RDHUP;
		break;
	case CON_STATE_WRITE:
		/* request write-fdevent only if we really need it
		 * - if we have data to write
		 * - if the socket is not writable yet
		 */
		if (!chunkqueue_is_empty(con->write_queue) &&
		    (con->is_writable == 0) &&
		    (con->traffic_limit_reached == 0)) {
			r |= FDEVENT_OUT;
		}
		/* fall through */
	case CON_STATE_READ_POST:
		if (con->conf.stream_request_body & FDEVENT_STREAM_REQUEST_POLLIN) {
			r |= FDEVENT_IN | FDEVENT_RDHUP;
		}
		break;
	case CON_STATE_CLOSE:
		r = FDEVENT_IN;
		break;
	default:
		break;
	}
	if (con->fd >= 0) {
		int events = connection_fdevent_get_interest(con->client_handler);
		if (con->is_readable < 0) {
			con->is_readable = 0;
			r |= FDEVENT_IN;
		}
		if (con->is_writable < 0) {
			con->is_writable = 0;
			r |= FDEVENT_OUT;
		}
		if (events & FDEVENT_RDHUP) {
			r |= FDEVENT_RDHUP;
		}
		if (r != events) {
			/* update timestamps when enabling interest in events */
			if ((r & FDEVENT_IN) && !(events & FDEVENT_IN)) {
				con->read_idle_ts = server_get_cur_ts();
			}
			if ((r & FDEVENT_OUT) && !(events & FDEVENT_OUT)) {
				con->write_request_ts = server_get_cur_ts();
			}
			connection_fdevent_set(con->client_handler, r);
		}
	}

}

static void connection_handle_fdevent_cb(int socketfd, int eventflag, void * event_arg) {
	server_update_cur_ts(time(NULL));
	UNUSED(socketfd);
	connection_request_ctx_t * req = (connection_request_ctx_t *)event_arg;
	/*call handler*/
	req->handler(req->srv, req->con, req->arg, connection_tpoolevent2fdevent((int)eventflag));
	if(req->con->is_appendjob) {
		connection_state_machine(req->srv, req->con);
		req->con->is_appendjob=0;
	}
}

static handler_t connection_handle_fdevent(server *srv, connection *con, void *context, int revents) {

	/*read event*/
	UNUSED(context);

	if (con->srv_socket->is_ssl) {
		/* ssl may read and write for both reads and writes */
		if (revents & (FDEVENT_IN | FDEVENT_OUT)) {
			con->is_readable = 1;
			con->is_writable = 1;
		}
	} else {
		if (revents & FDEVENT_IN) {
			con->is_readable = 1;
		}
		if (revents & FDEVENT_OUT) {
			con->is_writable = 1;
			/* we don't need the event twice */
		}
	}


	if (con->state == CON_STATE_READ) {
		connection_handle_read_state(srv, con);
	}

	if (con->state == CON_STATE_WRITE &&
	    !chunkqueue_is_empty(con->write_queue) &&
	    con->is_writable) {

		if (-1 == connection_handle_write(srv, con)) {
			connection_set_state(srv, con, CON_STATE_ERROR);

			log_error_write(srv, __FILE__, __LINE__, "ds",
					con->fd,
					"handle write failed.");
		}
	}

	if (con->state == CON_STATE_CLOSE) {
		/* flush the read buffers */
		connection_read_for_eos(srv, con);
	}


	/* attempt (above) to read data in kernel socket buffers
	 * prior to handling FDEVENT_HUP and FDEVENT_ERR */

	if ((revents & ~(FDEVENT_IN | FDEVENT_OUT)) && con->state != CON_STATE_ERROR) {
		if (con->state == CON_STATE_CLOSE) {
			con->close_timeout_ts = server_get_cur_ts() - (HTTP_LINGER_TIMEOUT+1);
		} else if (revents & FDEVENT_HUP) {
			connection_set_state(srv, con, CON_STATE_ERROR);
		} else if (revents & FDEVENT_RDHUP) {
			if (sock_addr_get_family(&con->dst_addr) == AF_UNIX) {
				/* future: will getpeername() on AF_UNIX properly check if still connected? */
				fdevent_event_clr(srv->ev, &con->fde_ndx, con->fd, FDEVENT_RDHUP);
				con->keep_alive = 0;
			} else if (fdevent_is_tcp_half_closed(con->fd)) {
				/* Success of fdevent_is_tcp_half_closed() after FDEVENT_RDHUP indicates TCP FIN received,
				 * but does not distinguish between client shutdown(fd, SHUT_WR) and client close(fd).
				 * Remove FDEVENT_RDHUP so that we do not spin on the ready event.
				 * However, a later TCP RST will not be detected until next write to socket.
				 * future: might getpeername() to check for TCP RST on half-closed sockets
				 * (without FDEVENT_RDHUP interest) when checking for write timeouts
				 * once a second in server.c, though getpeername() on Windows might not indicate this */
				fdevent_event_clr(srv->ev, &con->fde_ndx, con->fd, FDEVENT_RDHUP);
				con->keep_alive = 0;
			} else {
				/* Failure of fdevent_is_tcp_half_closed() indicates TCP RST
				 * (or unable to tell (unsupported OS), though should not
				 * be setting FDEVENT_RDHUP in that case) */
				connection_set_state(srv, con, CON_STATE_ERROR);
			}
		} else if (revents & FDEVENT_ERR) { /* error, connection reset */
			connection_set_state(srv, con, CON_STATE_ERROR);
		} else {
			log_error_write(srv, __FILE__, __LINE__, "sd",
					"connection closed: poll() -> ???", revents);
		}
	}

	connection_state_machine(srv, con);
	return HANDLER_FINISHED;
}


connection *connection_accept(server *srv, server_socket *srv_socket) {
	int cnt;
	sock_addr cnt_addr;
	size_t cnt_len = sizeof(cnt_addr); /*(size_t intentional; not socklen_t)*/

	/**
	 * check if we can still open a new connections
	 *
	 * see #1216
	 */

	if (mpool_get_usedcnt(srv->connspool) >= srv->max_conns) {
		return NULL;
	}

	cnt = fdevent_accept_listenfd(srv_socket->fd, (struct sockaddr *) &cnt_addr, &cnt_len);
	if (-1 == cnt) {
		switch (errno) {
		case EAGAIN:
#if EWOULDBLOCK != EAGAIN
		case EWOULDBLOCK:
#endif
		case EINTR:
			/* we were stopped _before_ we had a connection */
		case ECONNABORTED: /* this is a FreeBSD thingy */
			/* we were stopped _after_ we had a connection */
			break;
		case EMFILE:
			/* out of fds */
			break;
		default:
			log_error_write(srv, __FILE__, __LINE__, "ssd", "accept failed:", strerror(errno), errno);
		}
		return NULL;
	} else {
		if (sock_addr_get_family(&cnt_addr) != AF_UNIX) {
			network_accept_tcp_nagle_disable(cnt);
		}
		return connection_accepted(srv, srv_socket, &cnt_addr, cnt);
	}
}


/* 0: everything ok, -1: error, -2: con closed */
static int connection_read_cq(server *srv, connection *con, chunkqueue *cq, off_t max_bytes) {
	int len;
	char *mem = NULL;
	size_t mem_len = 0;
	int toread;
	force_assert(cq == con->read_queue);       /*(code transform assumption; minimize diff)*/
	force_assert(max_bytes == MAX_READ_LIMIT); /*(code transform assumption; minimize diff)*/

	/* default size for chunks is 4kb; only use bigger chunks if FIONREAD tells
	 *  us more than 4kb is available
	 * if FIONREAD doesn't signal a big chunk we fill the previous buffer
	 *  if it has >= 1kb free
	 */
	if (0 != fdevent_ioctl_fionread(con->fd, S_IFSOCK, &toread) || toread <= 4096) {
		toread = 4096;
	}
	else if (toread > MAX_READ_LIMIT) {
		toread = MAX_READ_LIMIT;
	}
	chunkqueue_get_memory(con->read_queue, &mem, &mem_len, 0, toread);

#if defined(__WIN32)
	len = recv(con->fd, mem, mem_len, 0);
#else
	len = read(con->fd, mem, mem_len);
#endif /* __WIN32 */
//	if(len < 0) fprintf(stderr, "read error:%s\n",strerror(errno) );

	chunkqueue_use_memory(con->read_queue, len > 0 ? len : 0);

	if (len < 0) {
		con->is_readable = 0;

#if defined(__WIN32)
		{
			int lastError = WSAGetLastError();
			switch (lastError) {
			case EAGAIN:
				return 0;
			case EINTR:
				/* we have been interrupted before we could read */
				con->is_readable = 1;
				return 0;
			case ECONNRESET:
				/* suppress logging for this error, expected for keep-alive */
				break;
			default:
				log_error_write(srv, __FILE__, __LINE__, "sd", "connection closed - recv failed: ", lastError);
				break;
			}
		}
#else /* __WIN32 */
		switch (errno) {
		case EAGAIN:
			return 0;
		case EINTR:
			/* we have been interrupted before we could read */
			con->is_readable = 1;
			return 0;
		case ECONNRESET:
			/* suppress logging for this error, expected for keep-alive */
			break;
		default:
			log_error_write(srv, __FILE__, __LINE__, "ssd", "connection closed - read failed: ", strerror(errno), errno);
			break;
		}
#endif /* __WIN32 */

		connection_set_state(srv, con, CON_STATE_ERROR);

		return -1;
	} else if (len == 0) {
		con->is_readable = 0;
		/* the other end close the connection -> KEEP-ALIVE */

		/* pipelining */

		return -2;
	} else if (len != (ssize_t) mem_len) {
		/* we got less then expected, wait for the next fd-event */

		con->is_readable = 0;
	}

	con->bytes_read += len;
	return 0;
}


static int connection_write_cq(server *srv, connection *con, chunkqueue *cq, off_t max_bytes) {
	return srv->network_backend_write(srv, con->fd, cq, max_bytes);
}

static ConEventHandler connection_client_handler_register(server *srv, connection *con) {
	return connection_fdevent_add(srv, con, con->fd, connection_handle_fdevent, NULL, 0);
}

connection *connection_accepted(server *srv, server_socket *srv_socket, sock_addr *cnt_addr, int cnt) {
		connection *con;

		server_increment_cur_fds();

		/* ok, we have the connection, register it */
#if 0
		log_error_write(srv, __FILE__, __LINE__, "sd",
				"appected()", cnt);
#endif
		con = connections_get_new_connection(srv);

		con->fd = cnt;
		con->fde_ndx = -1;
		con->client_handler = connection_client_handler_register(srv, con);
		con->network_read = connection_read_cq;
		con->network_write = connection_write_cq;

		connection_set_state(srv, con, CON_STATE_REQUEST_START);

		con->connection_start = server_get_cur_ts();
		con->dst_addr = *cnt_addr;
		buffer_copy_string(con->dst_addr_buf, inet_ntop_cache_get_ip(srv, &(con->dst_addr)));
		con->srv_socket = srv_socket;

		config_cond_cache_reset(srv, con);
		con->conditional_is_valid[COMP_SERVER_SOCKET] = 1;
		con->conditional_is_valid[COMP_HTTP_REMOTE_IP] = 1;

		if (-1 == fdevent_fcntl_set_nb_cloexec_sock(srv->ev, con->fd)) {
			log_error_write(srv, __FILE__, __LINE__, "ss", "fcntl failed: ", strerror(errno));
			connection_close(srv, con);
			return NULL;
		}
		buffer_copy_string_len(con->proto, CONST_STR_LEN("http"));
		if (HANDLER_GO_ON != plugins_call_handle_connection_accept(srv, con)) {
			connection_close(srv, con);
			return NULL;
		}
		return con;
}

ConEventHandler connection_fdevent_add(server *srv, connection *con, int fd, con_fdevent_handler handler, void *ctx, int events) {
	ConEventHandler instance = mpool_get(con->event_pool);
	force_assert(instance);
	instance->subscriber->fd = fd;
	instance->ctx.srv = srv;
	instance->ctx.con = con;
	instance->ctx.arg = ctx;
	instance->ctx.handler = handler;
	/*convert eventflag, and set event */
	instance->subscriber->eventflag = connection_fdevent2tpoolevent(events);
	event_tpool_add_result_t result = event_tpool_add_thread(srv->threadpool, con->state_machine->thread_num, instance->subscriber, &instance->ctx);
	force_assert(0<=result.result);

	instance->eventdata = result.event_handle;
	return instance;
}

void connection_fdevent_set(ConEventHandler ev, int events) {
	/*convert eventflag, and set event */
	ev->subscriber->eventflag = connection_fdevent2tpoolevent(events);
	/* there is a case "delete connection and receive msg at same time", don't need to care result*/
	event_tpool_update(ev->ctx.srv->threadpool, ev->eventdata, ev->subscriber, &ev->ctx);
}

void connection_fdevent_clr(ConEventHandler ev, int event) {
	ev->subscriber->eventflag &= ~connection_fdevent2tpoolevent(event);
	event_tpool_add_result_t result = event_tpool_update(ev->ctx.srv->threadpool, ev->eventdata, ev->subscriber, &ev->ctx);
	force_assert(0<=result.result);
}

void connection_fdevent_event_del(ConEventHandler ev) {
	event_tpool_del(ev->ctx.srv->threadpool, ev->subscriber->fd);
	connection * con = ev->ctx.con;
	mpool_release(con->event_pool, ev);
}

int connection_fdevent_get_interest(ConEventHandler ev) {
	if(!ev) return 0;
	return connection_fdevent2tpoolevent(ev->subscriber->eventflag);
}

static handler_t connection_sched_close_cb(server *srv, connection * con, void *context, int revents) {
	UNUSED(srv);
	UNUSED(con);
	UNUSED(revents);
	connection_fdevent_sched_close_directory((ConEventHandler)context);
	return HANDLER_GO_ON;
}

void connection_fdevent_sched_close(ConEventHandler ev) {
	ev->subscriber->eventflag |= EV_TPOOL_HUNGUP;
	ev->ctx.handler = connection_sched_close_cb;
	ev->ctx.arg = ev;
	connection_fdevent_set(ev, connection_tpoolevent2fdevent(ev->subscriber->eventflag));
}

void connection_fdevent_sched_close_directory(ConEventHandler ev) {
	connection_fdevent_event_del(ev);
	close(ev->subscriber->fd);
}

static void connection_state_machine_init(server *srv, connection *con) {
	if(!con->state_machine) {
		con->state_machine = state_machine_new(sizeof(state_event)/sizeof(state_event[0]), state_event, srv->threadpool);
		state_machine_set_state(con->state_machine, CON_STATE_CONNECT);
	}
}

void connection_state_machine_exit(server *srv, connection *con) {
	UNUSED(srv);
	state_machine_free(con->state_machine);
}

int connection_state_machine(server *srv, connection *con) {
	if (srv->srvconf.log_state_handling) {
		log_error_write(srv, __FILE__, __LINE__, "sds",
				"state at start",
				con->fd,
				connection_get_state(con->state));
	}

	http_connection_t http_con ={.srv=srv, .con=con, .ostate=connection_get_ostate(con)};
	state_machine_call_event(con->state_machine, CON_EVENT_RUN, &http_con, sizeof(http_con), NULL);
	return 0;
}


