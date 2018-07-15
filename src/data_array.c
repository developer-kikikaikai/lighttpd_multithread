#include "first.h"

#include "array.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static data_unset *data_array_copy(const data_unset *s) {
	data_array *src = (data_array *)s;
	data_array *ds = data_array_init();

	buffer_copy_buffer(ds->key, src->key);
	array_free(ds->value);
	ds->value = array_init_array(src->value);
	ds->is_index_key = src->is_index_key;
	return (data_unset *)ds;
}

static void data_array_free(data_unset *d) {
	data_type_free(TYPE_ARRAY, d);
}

static void data_array_reset(data_unset *d) {
	data_array *ds = (data_array *)d;

	/* reused array elements */
	buffer_reset(ds->key);
	array_reset(ds->value);
}

static int data_array_insert_dup(data_unset *dst, data_unset *src) {
	UNUSED(dst);

	data_type_get_method(src->type)->free(src);

	return 0;
}

static void data_array_print(const data_unset *d, int depth) {
	data_array *ds = (data_array *)d;

	array_print(ds->value, depth);
}

static void data_array_clone_free(void *clone_data) {
	data_array *ds = (data_array *)clone_data;

	buffer_free(ds->key);
	array_free(ds->value);

	free(clone_data);
}

static void *data_array_clone(void *base, size_t base_length) {
	UNUSED(base);
	data_array *ds = calloc(1, base_length);
	force_assert(NULL != ds);

	ds->type = TYPE_ARRAY;
	ds->key = buffer_init();
	ds->value = array_init();
	return ds;
}

void data_array_get_register(data_unset_register_data_t *data) {
	data->prime.copy = data_array_copy;
	data->prime.free = data_array_free;
	data->prime.reset = data_array_reset;
	data->prime.insert_dup = data_array_insert_dup;
	data->prime.print = data_array_print;
	data->prime.type = TYPE_ARRAY;
	data->size = sizeof(data_array);

	data->clone = data_array_clone;
	data->free = data_array_clone_free;
}

data_array *data_array_init(void) {
	return (data_array *)data_type_get(TYPE_ARRAY);
}
