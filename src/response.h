#ifndef _RESPONSE_H_
#define _RESPONSE_H_
#include "first.h"

#include "server.h"
#include "array.h"

#include <time.h>

typedef enum {
	RESP_CONTENT_TYPE,
	RESP_CONTENT_LENGTH,
	RESP_ACCEPT_RANGES,
	
} http_response_header_type_e;

int http_response_parse(server *srv, connection *con);
int http_response_write_header(server *srv, connection *con);

int response_header_insert(server *srv, connection *con, const char *key, size_t keylen, const char *value, size_t vallen);
void response_header_fixed_insert(server *srv, connection *con, http_response_fixed_type_e header_type);
int response_header_overwrite(server *srv, connection *con, const char *key, size_t keylen, const char *value, size_t vallen);
int response_header_append(server *srv, connection *con, const char *key, size_t keylen, const char *value, size_t vallen);
void response_header_fixed_append(server *srv, connection *con, const char *key, size_t keylen, const char *value, size_t vallen, http_response_fixed_type_e header_type);

typedef struct http_cgi_opts_t {
  int authorizer;
  int break_scriptfilename_for_php;
  buffer *docroot;
  buffer *strip_request_uri;
} http_cgi_opts;

enum {
  BACKEND_UNSET = 0,
  BACKEND_PROXY,
  BACKEND_CGI,
  BACKEND_FASTCGI,
  BACKEND_SCGI
};

typedef struct http_response_opts_t {
  int fdfmt;
  int backend;
  int authorizer;
  unsigned short local_redir;
  unsigned short xsendfile_allow;
  array *xsendfile_docroot;
  void *pdata;
  handler_t(*parse)(server *, connection *, struct http_response_opts_t *, buffer *, size_t);
  handler_t(*headers)(server *, connection *, struct http_response_opts_t *);
} http_response_opts;

typedef int (*http_cgi_header_append_cb)(void *vdata, const char *k, size_t klen, const char *v, size_t vlen);
int http_cgi_headers(server *srv, connection *con, http_cgi_opts *opts, http_cgi_header_append_cb cb, void *vdata);

handler_t http_response_parse_headers(server *srv, connection *con, http_response_opts *opts, buffer *hdrs);
handler_t http_response_read(server *srv, connection *con, http_response_opts *opts, buffer *b, int fd, ConEventHandler ev);
handler_t http_response_prepare(server *srv, connection *con);
int http_response_redirect_to_directory(server *srv, connection *con);
int http_response_handle_cachable(server *srv, connection *con, buffer * mtime);
void http_response_send_file (server *srv, connection *con, buffer *path);
void http_response_backend_done (server *srv, connection *con);
void http_response_backend_error (server *srv, connection *con);
void http_response_upgrade_read_body_unknown(server *srv, connection *con);

buffer * strftime_cache_get(server *srv, time_t last_mod);
void strftime_cache_init(void);
void strftime_cache_exit(void);

/*initialize header to remove extra copy*/
void http_response_fixed_header_register(void);
void response_header_fixed_overwrite(server *srv, connection *con, http_response_fixed_type_e header_type);
void http_response_fixed_header_unregister(void);

#endif
