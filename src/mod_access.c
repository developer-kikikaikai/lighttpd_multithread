#include "first.h"

#include "base.h"
#include "log.h"
#include "buffer.h"

#include "plugin.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
	array *access_allow;
	array *access_deny;
} plugin_config;

typedef struct {
	PLUGIN_DATA;

	plugin_config conf;
} plugin_data;

INIT_FUNC(mod_access_init) {
	plugin_data *p;

	p = calloc(1, sizeof(*p));

	return p;
}

FREE_FUNC(mod_access_free) {
	plugin_data *p = p_d;

	UNUSED(srv);

	if (!p) return HANDLER_GO_ON;

	plugin_config *s = &p->conf;
	array_free(s->access_allow);
	array_free(s->access_deny);
	free(p);

	return HANDLER_GO_ON;
}

SETDEFAULTS_FUNC(mod_access_set_defaults) {
	plugin_data *p = p_d;
	size_t i = 0;

	config_values_t cv[] = {
		{ "url.access-deny",             NULL, T_CONFIG_ARRAY, T_CONFIG_SCOPE_CONNECTION },
		{ "url.access-allow",            NULL, T_CONFIG_ARRAY, T_CONFIG_SCOPE_CONNECTION },
		{ NULL,                          NULL, T_CONFIG_UNSET, T_CONFIG_SCOPE_UNSET }
	};

	p->conf.access_deny = array_init();
	cv[0].destination = p->conf.access_deny;
	p->conf.access_allow = array_init();
	cv[1].destination = p->conf.access_allow;

	config_values_t *cv_tmp = calloc(sizeof(cv)/sizeof(cv[0]), sizeof(config_values_t));

	for (i = 0; i < srv->config_context->used; i++) {
		data_config const* config = (data_config const*)srv->config_context->data[i];
		
		//patch connection first
		if(config_patch_config(srv, config, cv, cv_tmp, i == 0 ? T_CONFIG_SCOPE_SERVER : T_CONFIG_SCOPE_CONNECTION)) {
			free(cv_tmp);
			return HANDLER_ERROR;
		}
	}

	if (!array_is_vlist(p->conf.access_deny)) {
		log_error_write(srv, __FILE__, __LINE__, "s",
				"unexpected value for url.access-deny; expected list of \"suffix\"");
		free(cv_tmp);
		return HANDLER_ERROR;
	}

	if (!array_is_vlist(p->conf.access_allow)) {
		log_error_write(srv, __FILE__, __LINE__, "s",
				"unexpected value for url.access-allow; expected list of \"suffix\"");
		free(cv_tmp);
		return HANDLER_ERROR;
	}

	free(cv_tmp);
	return HANDLER_GO_ON;
}

/**
 * URI handler
 *
 * we will get called twice:
 * - after the clean up of the URL and 
 * - after the pathinfo checks are done
 *
 * this handles the issue of trailing slashes
 */
URIHANDLER_FUNC(mod_access_uri_handler) {
	plugin_data *p = p_d;
	int s_len;
	size_t k;

	if (buffer_is_empty(con->uri.path)) return HANDLER_GO_ON;

	s_len = buffer_string_length(con->uri.path);

	if (con->conf.log_request_handling) {
		log_error_write(srv, __FILE__, __LINE__, "s",
				"-- mod_access_uri_handler called");
	}

	for (k = 0; k < p->conf.access_allow->used; ++k) {
		data_string *ds = (data_string *)p->conf.access_allow->data[k];
		int ct_len = buffer_string_length(ds->value);
		int allowed = 0;

		if (ct_len > s_len) continue;
		if (buffer_is_empty(ds->value)) continue;

		/* if we have a case-insensitive FS we have to lower-case the URI here too */

		if (con->conf.force_lowercase_filenames) {
			if (0 == strncasecmp(con->uri.path->ptr + s_len - ct_len, ds->value->ptr, ct_len)) {
				allowed = 1;
			}
		} else {
			if (0 == strncmp(con->uri.path->ptr + s_len - ct_len, ds->value->ptr, ct_len)) {
				allowed = 1;
			}
		}

		if (allowed) {
			return HANDLER_GO_ON;
		}
	}

	if (k > 0) { /* have access_allow but none matched */
		con->http_status = 403;
		con->mode = DIRECT;

		if (con->conf.log_request_handling) {
			log_error_write(srv, __FILE__, __LINE__, "sb",
				"url denied as failed to match any from access_allow", con->uri.path);
		}

		return HANDLER_FINISHED;
	}

	for (k = 0; k < p->conf.access_deny->used; k++) {
		data_string *ds = (data_string *)p->conf.access_deny->data[k];
		int ct_len = buffer_string_length(ds->value);
		int denied = 0;


		if (ct_len > s_len) continue;
		if (buffer_is_empty(ds->value)) continue;

		/* if we have a case-insensitive FS we have to lower-case the URI here too */

		if (con->conf.force_lowercase_filenames) {
			if (0 == strncasecmp(con->uri.path->ptr + s_len - ct_len, ds->value->ptr, ct_len)) {
				denied = 1;
			}
		} else {
			if (0 == strncmp(con->uri.path->ptr + s_len - ct_len, ds->value->ptr, ct_len)) {
				denied = 1;
			}
		}

		if (denied) {
			con->http_status = 403;
			con->mode = DIRECT;

			if (con->conf.log_request_handling) {
	 			log_error_write(srv, __FILE__, __LINE__, "sb", 
					"url denied as we match:", ds->value);
			}

			return HANDLER_FINISHED;
		}
	}

	/* not found */
	return HANDLER_GO_ON;
}


int mod_access_plugin_init(plugin *p);
int mod_access_plugin_init(plugin *p) {
	p->version     = LIGHTTPD_VERSION_ID;
	p->name        = buffer_init_string("access");

	p->init        = mod_access_init;
	p->set_defaults = mod_access_set_defaults;
	p->handle_uri_clean = mod_access_uri_handler;
	p->handle_subrequest_start  = mod_access_uri_handler;
	p->cleanup     = mod_access_free;

	p->data        = NULL;

	return 0;
}
