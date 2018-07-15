#include "first.h"

#include "array.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static data_unset *data_config_copy(const data_unset *s) {
	data_config *src = (data_config *)s;
	data_config *ds = data_config_init();

	ds->comp = src->comp;
	buffer_copy_buffer(ds->key, src->key);
	buffer_copy_buffer(ds->comp_tag, src->comp_tag);
	buffer_copy_buffer(ds->comp_key, src->comp_key);
	array_free(ds->value);
	ds->value = array_init_array(src->value);
	return (data_unset *)ds;
}

static void data_config_free(data_unset *d) {
	data_type_free(TYPE_CONFIG, d);
}

static void data_config_reset(data_unset *d) {
	data_config *ds = (data_config *)d;

	/* reused array elements */
	buffer_reset(ds->key);
	buffer_reset(ds->comp_tag);
	buffer_reset(ds->comp_key);
	array_reset(ds->value);
}

static int data_config_insert_dup(data_unset *dst, data_unset *src) {
	UNUSED(dst);

	data_type_get_method(TYPE_CONFIG)->free(src);

	return 0;
}

static void data_config_print(const data_unset *d, int depth) {
	data_config *ds = (data_config *)d;
	array *a = (array *)ds->value;
	size_t i;
	size_t maxlen;

	if (0 == ds->context_ndx) {
		fprintf(stdout, "config {\n");
	}
	else {
		if (ds->cond != CONFIG_COND_ELSE) {
			fprintf(stdout, "$%s %s \"%s\" {\n",
					ds->comp_key->ptr, ds->op->ptr, ds->string->ptr);
		} else {
			fprintf(stdout, "{\n");
		}
		array_print_indent(depth + 1);
		fprintf(stdout, "# block %d\n", ds->context_ndx);
	}
	depth ++;

	maxlen = array_get_max_key_length(a);
	for (i = 0; i < a->used; i ++) {
		data_unset *du = a->data[i];
		size_t len = buffer_string_length(du->key);
		size_t j;

		array_print_indent(depth);
		fprintf(stdout, "%s", du->key->ptr);
		for (j = maxlen - len; j > 0; j --) {
			fprintf(stdout, " ");
		}
		fprintf(stdout, " = ");
		data_type_get_method(du->type)->print(du, depth);
		fprintf(stdout, "\n");
	}

	fprintf(stdout, "\n");
	for (i = 0; i < ds->children.used; i ++) {
		data_config *dc = ds->children.data[i];

		/* only the 1st block of chaining */
		if (NULL == dc->prev) {
			fprintf(stdout, "\n");
			array_print_indent(depth);
			data_type_get_method(dc->type)->print((data_unset *) dc, depth);
			fprintf(stdout, "\n");
		}
	}

	depth --;
	array_print_indent(depth);
	fprintf(stdout, "}");
	if (0 != ds->context_ndx) {
		if (ds->cond != CONFIG_COND_ELSE) {
			fprintf(stdout, " # end of $%s %s \"%s\"",
					ds->comp_key->ptr, ds->op->ptr, ds->string->ptr);
		} else {
			fprintf(stdout, " # end of else");
		}
	}

	if (ds->next) {
		fprintf(stdout, "\n");
		array_print_indent(depth);
		fprintf(stdout, "else ");
		data_type_get_method(ds->next->type)->print((data_unset *)ds->next, depth);
	}
}

static void *data_config_clone(void *base, size_t base_length) {
	UNUSED(base);
	data_config *ds = calloc(1, base_length);

	ds->type = TYPE_CONFIG;
	ds->key = buffer_init();
	ds->op = buffer_init();
	ds->comp_tag = buffer_init();
	ds->comp_key = buffer_init();
	ds->value = array_init();
	vector_config_weak_init(&ds->children);

	return ds;
}

static void data_config_clone_free(void *d) {
	data_config *ds = (data_config *)d;

	buffer_free(ds->key);
	buffer_free(ds->op);
	buffer_free(ds->comp_tag);
	buffer_free(ds->comp_key);

	array_free(ds->value);
	vector_config_weak_clear(&ds->children);

	if (ds->string) buffer_free(ds->string);
#ifdef HAVE_PCRE_H
	if (ds->regex) pcre_free(ds->regex);
	if (ds->regex_study) pcre_free(ds->regex_study);
#endif

	free(d);
}

void data_config_get_register(data_unset_register_data_t *data) {
	fprintf(stderr, "%s\n", __func__);
	data->prime.copy = data_config_copy;
	data->prime.free = data_config_free;
	data->prime.reset = data_config_reset;
	data->prime.insert_dup = data_config_insert_dup;
	data->prime.print = data_config_print;
	data->prime.type = TYPE_CONFIG;
	data->size = sizeof(data_config);

	data->clone = data_config_clone;
	data->free = data_config_clone_free;
}

data_config *data_config_init(void) {
	return (data_config *)data_type_get(TYPE_CONFIG);
}
