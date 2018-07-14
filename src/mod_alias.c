#include "first.h"

#include "base.h"
#include "log.h"
#include "buffer.h"

#include "plugin.h"

#include <stdlib.h>
#include <string.h>

/* plugin config for all request/connections */
typedef struct {
	array *alias;
} plugin_config;

typedef struct {
	PLUGIN_DATA;

	plugin_config conf;
} plugin_data;

/* init the plugin data */
INIT_FUNC(mod_alias_init) {
	plugin_data *p;

	p = calloc(1, sizeof(*p));



	return p;
}

/* detroy the plugin data */
FREE_FUNC(mod_alias_free) {
	UNUSED(srv);
	plugin_data *p = p_d;

	if (!p) return HANDLER_GO_ON;

	array_free(p->conf.alias);
	free(p);

	return HANDLER_GO_ON;
}

/* handle plugin config and check values */

SETDEFAULTS_FUNC(mod_alias_set_defaults) {
	plugin_data *p = p_d;
	size_t i = 0;

	config_values_t cv[] = {
		{ "alias.url",                  NULL, T_CONFIG_ARRAY, T_CONFIG_SCOPE_CONNECTION },       /* 0 */
		{ NULL,                         NULL, T_CONFIG_UNSET,  T_CONFIG_SCOPE_UNSET }
	};

	if (!p) return HANDLER_ERROR;

	p->conf.alias = array_init();
	cv[0].destination = p->conf.alias;

	config_values_t *cv_tmp = calloc(sizeof(cv)/sizeof(cv[0]), sizeof(config_values_t));
	for (i = 0; i < srv->config_context->used; i++) {
		data_config const* config = (data_config const*)srv->config_context->data[i];

		if(config_patch_config(srv, config, cv, cv_tmp, i == 0 ? T_CONFIG_SCOPE_SERVER : T_CONFIG_SCOPE_CONNECTION)) {
			free(cv_tmp);
			return HANDLER_ERROR;
		}
	}

	if (!array_is_kvstring(p->conf.alias)) {
		log_error_write(srv, __FILE__, __LINE__, "s",
				"unexpected value for alias.url; expected list of \"urlpath\" => \"filepath\"");
		free(cv_tmp);
		return HANDLER_ERROR;
	}

	if (p->conf.alias->used >= 2) {
		const array *a = p->conf.alias;
		size_t j, k;

		for (j = 0; j < a->used; j ++) {
			const buffer *prefix = a->data[a->sorted[j]]->key;
			for (k = j + 1; k < a->used; k ++) {
				const buffer *key = a->data[a->sorted[k]]->key;

				if (buffer_string_length(key) < buffer_string_length(prefix)) {
					break;
				}
				if (memcmp(key->ptr, prefix->ptr, buffer_string_length(prefix)) != 0) {
					break;
				}
				/* ok, they have same prefix. check position */
				if (a->sorted[j] < a->sorted[k]) {
					log_error_write(srv, __FILE__, __LINE__, "SBSBS",
						"url.alias: `", key, "' will never match as `", prefix, "' matched first");
					free(cv_tmp);
					return HANDLER_ERROR;
				}
			}
		}
	}

	free(cv_tmp);
	return HANDLER_GO_ON;
}

PHYSICALPATH_FUNC(mod_alias_physical_handler) {
	plugin_data *p = p_d;
	int uri_len, basedir_len;
	char *uri_ptr;
	size_t k;

	if (buffer_is_empty(con->physical.path)) return HANDLER_GO_ON;

	/* not to include the tailing slash */
	basedir_len = buffer_string_length(con->physical.basedir);
	if ('/' == con->physical.basedir->ptr[basedir_len-1]) --basedir_len;
	uri_len = buffer_string_length(con->physical.path) - basedir_len;
	uri_ptr = con->physical.path->ptr + basedir_len;

	for (k = 0; k < p->conf.alias->used; k++) {
		data_string *ds = (data_string *)p->conf.alias->data[k];
		int alias_len = buffer_string_length(ds->key);

		if (alias_len > uri_len) continue;
		if (buffer_is_empty(ds->key)) continue;

		if (0 == (con->conf.force_lowercase_filenames ?
					strncasecmp(uri_ptr, ds->key->ptr, alias_len) :
					strncmp(uri_ptr, ds->key->ptr, alias_len))) {
			/* matched */

			buffer_copy_buffer(con->physical.basedir, ds->value);
			buffer_copy_buffer(con->tmp_buf, ds->value);
			buffer_append_string(con->tmp_buf, uri_ptr + alias_len);
			buffer_copy_buffer(con->physical.path, con->tmp_buf);

			return HANDLER_GO_ON;
		}
	}

	/* not found */
	return HANDLER_GO_ON;
}

/* this function is called at dlopen() time and inits the callbacks */

int mod_alias_plugin_init(plugin *p);
int mod_alias_plugin_init(plugin *p) {
	p->version     = LIGHTTPD_VERSION_ID;
	p->name        = buffer_init_string("alias");

	p->init           = mod_alias_init;
	p->handle_physical= mod_alias_physical_handler;
	p->set_defaults   = mod_alias_set_defaults;
	p->cleanup        = mod_alias_free;

	p->data        = NULL;

	return 0;
}
