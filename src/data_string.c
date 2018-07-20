#include "first.h"

#include "array.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <flyweight.h>

static void data_string_deepcopy(data_string *src, data_string *ds);
static void data_fixed_header_constructor(void *this, size_t size, void *input_parameter);
static int data_fixed_header_equall_operand(void *this, size_t size, void *input_parameter);
static void data_fixed_header_destructor(void *this);

static FlyweightFactory resp_factory;
static flyweight_methods_t resp_factory_method = {
	data_fixed_header_constructor,
	data_fixed_header_equall_operand,
	NULL,/* there is no setter*/
	data_fixed_header_destructor
};

static void data_response_deepcopy(data_string *src, data_string *ds) {
	buffer_copy_buffer(ds->value, src->value);
	ds->is_index_key = src->is_index_key;
}

static data_unset *data_response_copy(const data_unset *s) {
	data_string *src = (data_string *)s;
	data_string *ds = data_response_init();

	data_response_deepcopy(src, ds);
	return (data_unset *)ds;
}

static void data_response_free(data_unset *d) {
	data_type_free(TYPE_RESPONSE, d);
}

static void data_response_reset(data_unset *d) {
	data_string *ds = (data_string *)d;

	/* reused array elements */
	buffer_reset(ds->value);
}

static int data_response_insert_dup(data_unset *dst, data_unset *src) {
	data_string *ds_dst = (data_string *)dst;
	data_string *ds_src = (data_string *)src;

	if (!buffer_is_empty(ds_dst->value)) {
		buffer_append_string_len(ds_dst->value, CONST_STR_LEN("\r\n"));
		buffer_append_string_buffer(ds_dst->value, ds_dst->key);
		buffer_append_string_len(ds_dst->value, CONST_STR_LEN(": "));
		buffer_append_string_buffer(ds_dst->value, ds_src->value);
	} else {
		buffer_copy_buffer(ds_dst->value, ds_src->value);
	}

	data_type_get_method(src->type)->free(src);

	return 0;
}

static void data_string_print(const data_unset *d, int depth) {
	data_string *ds = (data_string *)d;
	size_t i, len;
	UNUSED(depth);

	/* empty and uninitialized strings */
	if (buffer_string_is_empty(ds->value)) {
		fputs("\"\"", stdout);
		return;
	}

	/* print out the string as is, except prepend " with backslash */
	putc('"', stdout);
	len = buffer_string_length(ds->value);
	for (i = 0; i < len; i++) {
		unsigned char c = ds->value->ptr[i];
		if (c == '"') {
			fputs("\\\"", stdout);
		} else {
			putc(c, stdout);
		}
	}
	putc('"', stdout);
}

static void * data_response_clone(void *base, size_t base_len) {
	UNUSED(base);
	data_string *ds = calloc(1, base_len);
	force_assert(NULL != ds);

	ds->type = TYPE_RESPONSE;
	ds->value = buffer_init();
	return ds;
}

static void data_response_clone_free(void *d) {
	data_string *ds = (data_string *)d;

	buffer_free(ds->value);

	free(d);
}

void data_response_get_register(data_unset_register_data_t *data) {
	data->prime.copy = data_response_copy;
	data->prime.free = data_response_free;
	data->prime.reset = data_response_reset;
	data->prime.insert_dup = data_response_insert_dup;
	data->prime.print = data_string_print;
	data->prime.type = TYPE_RESPONSE;

	data->clone = data_response_clone;
	data->free = data_response_clone_free;
}

static void data_string_deepcopy(data_string *src, data_string *ds) {
	buffer_copy_buffer(ds->key, src->key);
	data_response_deepcopy(src, ds);
}

static data_unset *data_string_copy(const data_unset *s) {
	data_string *src = (data_string *)s;
	data_string *ds = data_string_init();

	data_string_deepcopy(src, ds);
	return (data_unset *)ds;
}

static void data_string_free(data_unset *d) {
	data_type_free(TYPE_STRING, d);
}

static void data_string_unfree(data_unset *d) {
	UNUSED(d);
}

static void data_string_reset(data_unset *d) {
	data_string *ds = (data_string *)d;

	/* reused array elements */
	buffer_reset(ds->key);
	buffer_reset(ds->value);
}

static int data_string_insert_dup(data_unset *dst, data_unset *src) {
	data_string *ds_dst = (data_string *)dst;
	data_string *ds_src = (data_string *)src;

	if (!buffer_is_empty(ds_dst->value)) {
		buffer_append_string_len(ds_dst->value, CONST_STR_LEN(", "));
		buffer_append_string_buffer(ds_dst->value, ds_src->value);
	} else {
		buffer_copy_buffer(ds_dst->value, ds_src->value);
	}

	data_type_get_method(src->type)->free(src);

	return 0;
}

static void * data_string_clone(void *base, size_t base_len) {
	UNUSED(base);
	data_string *ds = data_response_clone(base, base_len);
	force_assert(NULL != ds);

	ds->type = TYPE_STRING;
	ds->key = buffer_init();
	return ds;
}

static void data_string_clone_free(void *d) {
	data_string *ds = (data_string *)d;

	buffer_free(ds->key);
	data_response_clone_free(d);
}

void data_string_get_register(data_unset_register_data_t *data) {
	data->prime.copy = data_string_copy;
	data->prime.free = data_string_free;
	data->prime.reset = data_string_reset;
	data->prime.insert_dup = data_string_insert_dup;
	data->prime.print = data_string_print;
	data->prime.type = TYPE_STRING;

	data->size = sizeof(data_string);
	data->clone = data_string_clone;
	data->free = data_string_clone_free;
}

static void data_fixed_header_constructor(void *this, size_t size, void *input_parameter) {
	UNUSED(size);
	data_fixed_header *ds = (data_fixed_header *)this;
	data_fixed_header *src = (data_fixed_header *)input_parameter;

	ds->type = TYPE_FIXED_HEADER;
	ds->key = buffer_init();
	ds->value = buffer_init();
	ds->header_type = src->header_type;
}

static int data_fixed_header_equall_operand(void *this, size_t size, void *input_parameter) {
	UNUSED(size);
	data_fixed_header *ds = (data_fixed_header *)this;
	data_fixed_header *src = (data_fixed_header *)input_parameter;
	return (ds->header_type==src->header_type);
}

static void data_fixed_header_destructor(void *this) {
	data_fixed_header * ds = (data_fixed_header *) this;
	buffer_free(ds->key);
	buffer_free(ds->value);
}

static data_unset *data_fixed_header_get(const data_unset *s) {
	if(!resp_factory) resp_factory = flyweight_factory_new(sizeof(data_fixed_header), 0, &resp_factory_method);/*Beacuse we only get 1st time, we don't need lock*/
	
	return (data_unset *)flyweight_get(resp_factory, (void *)s);
}

static void * data_fixed_header_clone(void *base, size_t base_len) {
	UNUSED(base_len);
	return data_fixed_header_get(base);
}

void data_fixed_header_get_register(data_unset_register_data_t *data) {
	data_response_get_register(data);

	data->prime.copy = data_fixed_header_get;
	data->prime.free = data_string_unfree;
	data->prime.reset = data_string_unfree;
	data->prime.type = TYPE_FIXED_HEADER;

	data->size = sizeof(data_fixed_header);
	data->clone = data_fixed_header_clone;
}

data_string *data_string_init(void) {
	return (data_string *)data_type_get(TYPE_STRING);
}

data_string *data_response_init(void) {
	return (data_string *)data_type_get(TYPE_RESPONSE);
}

void data_fixed_header_exit(void) {
	flyweight_factory_free(resp_factory);
	buffer_exit();
}
