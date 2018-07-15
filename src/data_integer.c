#include "first.h"

#include "array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static data_unset *data_integer_copy(const data_unset *s) {
	data_integer *src = (data_integer *)s;
	data_integer *ds = data_integer_init();

	buffer_copy_buffer(ds->key, src->key);
	ds->is_index_key = src->is_index_key;
	ds->value = src->value;
	return (data_unset *)ds;
}

static void data_integer_free(data_unset *d) {
	data_type_free(TYPE_INTEGER, d);
}

static void data_integer_reset(data_unset *d) {
	data_integer *ds = (data_integer *)d;

	/* reused integer elements */
	buffer_reset(ds->key);
	ds->value = 0;
}

static int data_integer_insert_dup(data_unset *dst, data_unset *src) {
	UNUSED(dst);

	data_type_get_method(src->type)->free(src);

	return 0;
}

static void data_integer_print(const data_unset *d, int depth) {
	data_integer *ds = (data_integer *)d;
	UNUSED(depth);

	fprintf(stdout, "%d", ds->value);
}

static void data_integer_clone_free(void *d) {
	data_integer *ds = (data_integer *)d;

	buffer_free(ds->key);

	free(d);
}

static void * data_integer_clone(void *base, size_t base_len) {
	UNUSED(base);
	data_integer *ds = calloc(1, base_len);
	force_assert(NULL != ds);

	ds->type = TYPE_INTEGER;
	ds->key = buffer_init();
	ds->value = 0;
	return ds;
}

void data_integer_get_register(data_unset_register_data_t *data) {
	data->prime.copy = data_integer_copy;
	data->prime.free = data_integer_free;
	data->prime.reset = data_integer_reset;
	data->prime.insert_dup = data_integer_insert_dup;
	data->prime.print = data_integer_print;

	data->prime.type = TYPE_INTEGER;
	data->clone = data_integer_clone;
	data->free = data_integer_clone_free;
}

data_integer *data_integer_init(void) {
	return (data_integer *)data_type_get(TYPE_INTEGER);
}
