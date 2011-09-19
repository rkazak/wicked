/*
 * Common DBus functions
 *
 * Copyright (C) 2011 Olaf Kirch <okir@suse.de>
 */

#include <dbus/dbus.h>
#include <sys/poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <wicked/util.h>
#include <wicked/logging.h>
#include "socket_priv.h"
#include "dbus-common.h"
#include "dbus-dict.h"

#define TRACE_ENTER()		ni_debug_dbus("%s()", __FUNCTION__)
#define TP()			ni_debug_dbus("TP - %s:%u", __FUNCTION__, __LINE__)

static ni_intmap_t      __ni_dbus_error_map[] = {
	{ "org.freedesktop.DBus.Error.AccessDenied",	EACCES },
	{ "org.freedesktop.DBus.Error.InvalidArgs",	EINVAL },
	{ "org.freedesktop.DBus.Error.UnknownMethod",	EOPNOTSUPP },

	{ NULL }
};


int
ni_dbus_translate_error(const DBusError *err, const ni_intmap_t *error_map)
{
	unsigned int errcode;

	ni_debug_dbus("%s(%s, msg=%s)", __FUNCTION__, err->name, err->message);

	if (error_map && ni_parse_int_mapped(err->name, error_map, &errcode) >= 0)
		return errcode;

	if (ni_parse_int_mapped(err->name, __ni_dbus_error_map, &errcode) >= 0)
		return errcode;

	ni_warn("Cannot translate DBus error <%s>", err->name);
	return EIO;
}

/*
 * Deserialize message
 *
 * We need this wrapper function because dbus_message_get_args_valist
 * does not copy any strings, but returns char pointers that point at
 * the message body. Which is bad if you want to access these strings
 * after you've freed the message.
 */
int
ni_dbus_message_get_args(ni_dbus_message_t *msg, ...)
{
	DBusError error;
	va_list ap;
	int rv = 0, type;

	TRACE_ENTER();
	dbus_error_init(&error);
	va_start(ap, msg);

	type = va_arg(ap, int);
	if (type
	 && !dbus_message_get_args_valist(msg, &error, type, ap)) {
		ni_error("%s: unable to retrieve msg data", __FUNCTION__);
		rv = -EINVAL;
		goto done;
	}

	while (type) {
		char **data = va_arg(ap, char **);

		switch (type) {
		case DBUS_TYPE_STRING:
		case DBUS_TYPE_OBJECT_PATH:
			if (data && *data)
				*data = xstrdup(*data);
			break;
		}

		type = va_arg(ap, int);
	}

done:
	va_end(ap);
	return rv;
}

/*
 * Deserialize message and store data in an array of variant objects
 */
int
ni_dbus_message_get_args_variants(ni_dbus_message_t *msg, ni_dbus_variant_t *argv, unsigned int max_args)
{
	DBusMessageIter iter;
	unsigned int argc = 0;

	dbus_message_iter_init(msg, &iter);
	for (argc = 0; argc < max_args; ++argc) {
		DBusMessageIter *iter_p = &iter, iter_val;

		/* As a matter of convenience to the coder,
		 * automatically drill into arguments that are wrapped in a variant */
		if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
			dbus_message_iter_recurse(&iter, &iter_val);
			iter_p = &iter_val;
		}
		if (!ni_dbus_message_iter_get_variant_data(iter_p, &argv[argc]))
			return -1;
		if (!dbus_message_iter_next(&iter))
			break;
	}

	return argc;
}

/*
 * Helper function for processing a DBusDict
 */
static inline const struct ni_dbus_dict_entry_handler *
__ni_dbus_get_property_handler(const struct ni_dbus_dict_entry_handler *handlers, const char *name)
{
	const struct ni_dbus_dict_entry_handler *h;

	for (h = handlers; h->type; ++h) {
		if (!strcmp(h->name, name))
			return h;
	}
	return NULL;
}

int
ni_dbus_process_properties(DBusMessageIter *iter, const struct ni_dbus_dict_entry_handler *handlers, void *user_object)
{
	struct ni_dbus_dict_entry entry;
	int rv = 0;

	TRACE_ENTER();
	while (ni_dbus_dict_get_entry(iter, &entry)) {
		const struct ni_dbus_dict_entry_handler *h;
		const ni_dbus_variant_t *v = &entry.datum;

#if 0
		if (v->type == DBUS_TYPE_ARRAY) {
			ni_debug_dbus("++%s -- array of type %c", entry.key, v->array.type);
		} else {
			ni_debug_dbus("++%s -- type %c", entry.key, v->type);
		}
#endif

		if (!(h = __ni_dbus_get_property_handler(handlers, entry.key))) {
			ni_debug_dbus("%s: ignore unknown dict element \"%s\"", __FUNCTION__, entry.key);
			continue;
		}

		if (h->type != v->type
		 || (h->type == DBUS_TYPE_ARRAY && h->array_type != v->array.element_type)) {
			ni_error("%s: unexpected type for dict element \"%s\"", __FUNCTION__, entry.key);
			rv = -EINVAL;
			break;
		}

		if (h->type == DBUS_TYPE_ARRAY && h->array_len_max != 0
		 && (v->array.len < h->array_len_min || h->array_len_max < v->array.len)) {
			ni_error("%s: unexpected array length %u for dict element \"%s\"",
					__FUNCTION__, (int) v->array.len, entry.key);
			rv = -EINVAL;
			break;
		}

		if (h->set)
			h->set(&entry, user_object);
	}

	return rv;
}

/*
 * Get/set functions for variant values
 */
static inline void
__ni_dbus_variant_change_type(ni_dbus_variant_t *var, int new_type)
{
	if (var->type == new_type)
		return;
	if (var->type != DBUS_TYPE_INVALID) {
		if (var->type == DBUS_TYPE_STRING
		 || var->type == DBUS_TYPE_OBJECT_PATH
		 || var->type == DBUS_TYPE_ARRAY)
			ni_dbus_variant_destroy(var);
	}
	var->type = new_type;
}

void
ni_dbus_variant_set_string(ni_dbus_variant_t *var, const char *value)
{
	__ni_dbus_variant_change_type(var, DBUS_TYPE_BOOLEAN);
	ni_string_dup(&var->string_value, value);
}

void
ni_dbus_variant_set_bool(ni_dbus_variant_t *var, dbus_bool_t value)
{
	__ni_dbus_variant_change_type(var, DBUS_TYPE_BOOLEAN);
	var->bool_value = value;
}

void
ni_dbus_variant_set_byte(ni_dbus_variant_t *var, unsigned char value)
{
	__ni_dbus_variant_change_type(var, DBUS_TYPE_BYTE);
	var->byte_value = value;
}

void
ni_dbus_variant_set_uint16(ni_dbus_variant_t *var, uint16_t value)
{
	__ni_dbus_variant_change_type(var, DBUS_TYPE_UINT16);
	var->uint16_value = value;
}

void
ni_dbus_variant_set_int16(ni_dbus_variant_t *var, int16_t value)
{
	__ni_dbus_variant_change_type(var, DBUS_TYPE_INT16);
	var->int16_value = value;
}

void
ni_dbus_variant_set_uint32(ni_dbus_variant_t *var, uint32_t value)
{
	__ni_dbus_variant_change_type(var, DBUS_TYPE_UINT32);
	var->uint32_value = value;
}

void
ni_dbus_variant_set_int32(ni_dbus_variant_t *var, int32_t value)
{
	__ni_dbus_variant_change_type(var, DBUS_TYPE_INT32);
	var->int32_value = value;
}

void
ni_dbus_variant_set_uint64(ni_dbus_variant_t *var, uint64_t value)
{
	__ni_dbus_variant_change_type(var, DBUS_TYPE_UINT64);
	var->uint64_value = value;
}

void
ni_dbus_variant_set_int64(ni_dbus_variant_t *var, int64_t value)
{
	__ni_dbus_variant_change_type(var, DBUS_TYPE_INT64);
	var->int64_value = value;
}

/*
 * Helper function for handling arrays
 */
#define NI_DBUS_ARRAY_CHUNK		32
#define NI_DBUS_ARRAY_ALLOCATION(len)	(((len) + NI_DBUS_ARRAY_CHUNK - 1) & ~(NI_DBUS_ARRAY_CHUNK - 1))
static inline void
__ni_dbus_array_grow(ni_dbus_variant_t *var, size_t element_size, unsigned int grow_by)
{
	unsigned int max = NI_DBUS_ARRAY_ALLOCATION(var->array.len);
	unsigned int len = var->array.len;

	if (len + grow_by >= max) {
		void *new_data;

		max = NI_DBUS_ARRAY_ALLOCATION(len + grow_by);
		new_data = xcalloc(max, element_size);
		if (new_data == NULL)
			ni_fatal("%s: out of memory try to grow array to %u elements",
					__FUNCTION__, len + grow_by);

		memcpy(new_data, var->byte_array_value, len * element_size);
		free(var->byte_array_value);
		var->byte_array_value = new_data;
	}
}

void
ni_dbus_variant_set_byte_array(ni_dbus_variant_t *var,
				unsigned int len, const unsigned char *data)
{
	ni_dbus_variant_destroy(var);
	var->type = DBUS_TYPE_ARRAY;
	var->array.element_type = DBUS_TYPE_BYTE;

	__ni_dbus_array_grow(var, sizeof(unsigned char), len);
	if (len)
		memcpy(var->byte_array_value, data, len);
}

dbus_bool_t
ni_dbus_variant_append_byte_array(ni_dbus_variant_t *var, unsigned char byte)
{
	if (var->type != DBUS_TYPE_ARRAY
	 || var->array.element_type != DBUS_TYPE_BYTE)
		return FALSE;

	__ni_dbus_array_grow(var, sizeof(unsigned char), 1);
	var->byte_array_value[var->array.len++] = byte;
	return TRUE;
}

void
ni_dbus_variant_set_string_array(ni_dbus_variant_t *var,
				unsigned int len, const char **data)
{
	ni_dbus_variant_destroy(var);
	var->type = DBUS_TYPE_ARRAY;
	var->array.element_type = DBUS_TYPE_STRING;

	__ni_dbus_array_grow(var, sizeof(char *), len);
	if (len) {
		unsigned int i;

		for (i = 0; i < len; ++i)
			var->string_array_value[i] = xstrdup(data[i]?: "");
	}
}

dbus_bool_t
ni_dbus_variant_append_string_array(ni_dbus_variant_t *var, const char *string)
{
	unsigned int len = var->array.len;

	if (var->type != DBUS_TYPE_ARRAY
	 || var->array.element_type != DBUS_TYPE_STRING)
		return FALSE;

	__ni_dbus_array_grow(var, sizeof(char *), 1);
	var->string_array_value[len] = xstrdup(string?: "");
	var->array.len++;

	return TRUE;
}

void
ni_dbus_variant_destroy(ni_dbus_variant_t *var)
{
	if (var->type == DBUS_TYPE_STRING
	 || var->type == DBUS_TYPE_OBJECT_PATH)
		ni_string_free(&var->string_value);
	else if (var->type == DBUS_TYPE_ARRAY) {
		unsigned int i;

		switch (var->array.element_type) {
		case DBUS_TYPE_BYTE:
			free(var->byte_array_value);
			break;
		case DBUS_TYPE_STRING:
			for (i = 0; i < var->array.len; ++i)
				free(var->string_array_value[i]);
			free(var->string_array_value);
			break;
		}
	}
	memset(var, 0, sizeof(*var));
	var->type = DBUS_TYPE_INVALID;
}

const char *
ni_dbus_variant_sprint(const ni_dbus_variant_t *var)
{
	static char buffer[256];

	switch (var->type) {
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
		return var->string_value;

	case DBUS_TYPE_BYTE:
		snprintf(buffer, sizeof(buffer), "0x%02x", var->byte_value);
		break;

	case DBUS_TYPE_BOOLEAN:
		return var->bool_value? "true" : "false";
		break;

	case DBUS_TYPE_INT16:
		snprintf(buffer, sizeof(buffer), "%d", var->int16_value);
		break;

	case DBUS_TYPE_UINT16:
		snprintf(buffer, sizeof(buffer), "%u", var->uint16_value);
		break;

	case DBUS_TYPE_INT32:
		snprintf(buffer, sizeof(buffer), "%d", var->int32_value);
		break;

	case DBUS_TYPE_UINT32:
		snprintf(buffer, sizeof(buffer), "%u", var->uint32_value);
		break;

	case DBUS_TYPE_INT64:
		snprintf(buffer, sizeof(buffer), "%lld", (long long) var->int64_value);
		break;

	case DBUS_TYPE_UINT64:
		snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long) var->uint64_value);
		break;

	default:
		return "<unknown type>";
	}


	return buffer;
}

const char *
ni_dbus_variant_signature(const ni_dbus_variant_t *var)
{
	static char buffer[64];
	const char *sig;

	sig = ni_dbus_type_as_string(var->type);
	if (sig)
		return sig;

	switch (var->type) {
	case DBUS_TYPE_ARRAY:
		strcpy(buffer, DBUS_TYPE_ARRAY_AS_STRING);
		switch (var->array.element_type) {
		case DBUS_TYPE_BYTE:
			return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_BYTE_AS_STRING;
		case DBUS_TYPE_STRING:
			return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_STRING_AS_STRING;
		case DBUS_TYPE_VARIANT:
			return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_VARIANT_AS_STRING;
		}
		break;
	}

	return NULL;
}

/*
 * Translate basic dbus types to signature strings
 */
static const char * __ni_dbus_basic_type_as_string[256] = {
[DBUS_TYPE_BYTE]	= DBUS_TYPE_BYTE_AS_STRING,
[DBUS_TYPE_BOOLEAN]	= DBUS_TYPE_BOOLEAN_AS_STRING,
[DBUS_TYPE_INT16]	= DBUS_TYPE_INT16_AS_STRING,
[DBUS_TYPE_UINT16]	= DBUS_TYPE_UINT16_AS_STRING,
[DBUS_TYPE_INT32]	= DBUS_TYPE_INT32_AS_STRING,
[DBUS_TYPE_UINT32]	= DBUS_TYPE_UINT32_AS_STRING,
[DBUS_TYPE_INT64]	= DBUS_TYPE_INT64_AS_STRING,
[DBUS_TYPE_UINT64]	= DBUS_TYPE_UINT64_AS_STRING,
[DBUS_TYPE_DOUBLE]	= DBUS_TYPE_DOUBLE_AS_STRING,
[DBUS_TYPE_STRING]	= DBUS_TYPE_STRING_AS_STRING,
[DBUS_TYPE_OBJECT_PATH]	= DBUS_TYPE_OBJECT_PATH_AS_STRING,
};

const char *
ni_dbus_type_as_string(int type)
{
	if (type < 0 || type >= 256)
		return NULL;
	return __ni_dbus_basic_type_as_string[(unsigned int) type];
}

/*
 * Offsets of all elements in the variant struct
 */
unsigned int
__ni_dbus_variant_offsets[256] = {
[DBUS_TYPE_BYTE]		= offsetof(ni_dbus_variant_t, byte_value),
[DBUS_TYPE_BOOLEAN]		= offsetof(ni_dbus_variant_t, bool_value),
[DBUS_TYPE_STRING]		= offsetof(ni_dbus_variant_t, string_value),
[DBUS_TYPE_INT16]		= offsetof(ni_dbus_variant_t, int16_value),
[DBUS_TYPE_UINT16]		= offsetof(ni_dbus_variant_t, uint16_value),
[DBUS_TYPE_INT32]		= offsetof(ni_dbus_variant_t, int32_value),
[DBUS_TYPE_UINT32]		= offsetof(ni_dbus_variant_t, uint32_value),
[DBUS_TYPE_INT64]		= offsetof(ni_dbus_variant_t, int64_value),
[DBUS_TYPE_UINT64]		= offsetof(ni_dbus_variant_t, uint64_value),
};
