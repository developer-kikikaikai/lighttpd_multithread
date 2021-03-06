#ifndef ARRAY_H
#define ARRAY_H
#include "first.h"

#ifdef HAVE_PCRE_H
# include <pcre.h>
#endif

#include "buffer.h"
#include "vector.h"

#include <stdlib.h>

typedef enum { TYPE_UNSET, TYPE_STRING, TYPE_RESPONSE, TYPE_FIXED_HEADER, TYPE_AUTH, TYPE_ARRAY, TYPE_INTEGER, TYPE_CONFIG, TYPE_SIZEOF } data_type_t;
#define DATA_UNSET_PRIME \
	data_type_t type; \
	struct data_unset *(*copy)(const struct data_unset *src); \
	void (* free)(struct data_unset *p); \
	void (* reset)(struct data_unset *p); \
	int (*insert_dup)(struct data_unset *dst, struct data_unset *src); \
	void (*print)(const struct data_unset *p, int depth);

#define DATA_UNSET \
	data_type_t type;\
	buffer *key; \
	int is_index_key
 /* 1 if key is a array index (autogenerated keys) */ 

typedef struct data_unset_prime {
	DATA_UNSET_PRIME
} data_unset_prime;

typedef struct data_unset {
	DATA_UNSET;
} data_unset;

typedef struct {
	data_unset  **data;

	size_t *sorted;

	size_t used; /* <= SSIZE_MAX */
	size_t size;

	size_t unique_ndx;
} array;

typedef struct {
	DATA_UNSET;

	buffer *value;
} data_string;

data_string *data_string_init(void);
data_string *data_response_init(void);
void data_response_exit(void);

typedef enum {
	RESP_FIXED_HEADER_CONNECTION_CLOSE,
	RESP_FIXED_HEADER_CONNECTION_KEEPALIVE,
	RESP_FIXED_HEADER_CONNECTION_UPGRAGE,
	RESP_FIXED_HEADER_TRANSFER_ENCODING_CHUNKED,
	RESP_FIXED_HEADER_ACCEPT_RANGES_BYTES,
} http_response_fixed_type_e;

typedef struct {
	DATA_UNSET;

	buffer *value;
	http_response_fixed_type_e header_type;
} data_fixed_header;

//Don't neet data_fixed_header_init because data_fixed_header always use static value

typedef struct {
	DATA_UNSET;

	array *value;
} data_array;

data_array *data_array_init(void);

/**
 * possible compare ops in the configfile parser
 */
typedef enum {
	CONFIG_COND_UNSET,
	CONFIG_COND_EQ,      /** == */
	CONFIG_COND_MATCH,   /** =~ */
	CONFIG_COND_NE,      /** != */
	CONFIG_COND_NOMATCH, /** !~ */
	CONFIG_COND_ELSE     /** (always true if reached) */
} config_cond_t;

/**
 * possible fields to match against
 */
typedef enum {
	COMP_UNSET,
	COMP_SERVER_SOCKET,
	COMP_HTTP_URL,
	COMP_HTTP_HOST,
	COMP_HTTP_REFERER,        /*(subsumed by COMP_HTTP_REQUEST_HEADER)*/
	COMP_HTTP_USER_AGENT,     /*(subsumed by COMP_HTTP_REQUEST_HEADER)*/
	COMP_HTTP_LANGUAGE,       /*(subsumed by COMP_HTTP_REQUEST_HEADER)*/
	COMP_HTTP_COOKIE,         /*(subsumed by COMP_HTTP_REQUEST_HEADER)*/
	COMP_HTTP_REMOTE_IP,
	COMP_HTTP_QUERY_STRING,
	COMP_HTTP_SCHEME,
	COMP_HTTP_REQUEST_METHOD,
	COMP_HTTP_REQUEST_HEADER,

	COMP_LAST_ELEMENT
} comp_key_t;

/* $HTTP["host"] ==    "incremental.home.kneschke.de" { ... }
 * for print:   comp_key      op    string
 * for compare: comp          cond  string/regex
 */

typedef struct data_config data_config;
DEFINE_TYPED_VECTOR_NO_RELEASE(config_weak, data_config*);

struct data_config {
	DATA_UNSET;

	array *value;

	buffer *comp_tag;
	buffer *comp_key;
	comp_key_t comp;

	config_cond_t cond;
	buffer *op;

	int context_ndx; /* more or less like an id */
	vector_config_weak children;
	/* nested */
	data_config *parent;
	/* for chaining only */
	data_config *prev;
	data_config *next;

	buffer *string;
#ifdef HAVE_PCRE_H
	pcre   *regex;
	pcre_extra *regex_study;
#endif
};

data_config *data_config_init(void);

typedef struct {
	DATA_UNSET;

	int value;
} data_integer;

data_integer *data_integer_init(void);

array *array_init(void);
array *array_init_array(array *a);
void array_free(array *a);
void array_reset(array *a);
void array_insert_unique(array *a, data_unset *entry);
data_unset *array_pop(array *a); /* only works on "simple" lists with autogenerated keys */
int array_is_vlist(array *a);
int array_is_kvany(array *a);
int array_is_kvarray(array *a);
int array_is_kvstring(array *a);
int array_print(array *a, int depth);
data_unset *array_get_unused_element(array *a, data_type_t t);
#define array_get_element(a, key) array_get_element_klen((a), (key), sizeof(key)-1)
data_unset *array_get_element_klen(const array *a, const char *key, size_t klen);
data_unset *array_extract_element_klen(array *a, const char *key, size_t klen); /* removes found entry from array */
void array_set_key_value(array *hdrs, const char *key, size_t key_len, const char *value, size_t val_len);
void array_replace(array *a, data_unset *entry);
int array_strcasecmp(const char *a, size_t a_len, const char *b, size_t b_len);
void array_print_indent(int depth);
size_t array_get_max_key_length(array *a);


void data_type_register_all(void);

typedef struct {
	data_unset_prime prime;
	int size;
	void *(*clone)(void *base,size_t base_length);
	void (*free)(void *clone_data);
} data_unset_register_data_t;

void data_type_get_register(int type, data_unset_register_data_t *data);
void data_string_get_register(data_unset_register_data_t *data);
void data_response_get_register(data_unset_register_data_t *data);
void data_fixed_header_get_register(data_unset_register_data_t *data);
void data_array_get_register(data_unset_register_data_t *data);
void data_integer_get_register(data_unset_register_data_t *data);
void data_config_get_register(data_unset_register_data_t *data);

data_unset * data_type_get(int type);
data_unset_prime * data_type_get_method(int type);
void data_type_free(int type, data_unset * cloned_data);
void data_type_unregister_all(void);

void data_fixed_header_exit(void);
#endif
