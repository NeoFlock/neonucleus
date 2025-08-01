#include "neonucleus.h"

nn_value nn_values_nil(void) {
    return (nn_value) {.tag = NN_VALUE_NIL};
}

nn_value nn_values_integer(nn_integer_t integer) {
    return (nn_value) {.tag = NN_VALUE_INT, .integer = integer};
}

nn_value nn_values_number(double num) {
    return (nn_value) {.tag = NN_VALUE_NUMBER, .number = num};
}

nn_value nn_values_boolean(nn_bool_t boolean) {
    return (nn_value) {.tag = NN_VALUE_BOOL, .boolean = boolean};
}

nn_value nn_values_cstring(const char *string) {
    return (nn_value) {.tag = NN_VALUE_CSTR, .cstring = string};
}

nn_value nn_values_string(nn_Alloc *alloc, const char *string, nn_size_t len) {
    char *buf = nn_alloc(alloc, len+1);
    if(buf == NULL) {
        return nn_values_nil();
    }
    nn_memcpy(buf, string, len);
    buf[len] = '\0';

    nn_string *s = nn_alloc(alloc, sizeof(nn_string));
    if(s == NULL) {
        nn_dealloc(alloc, buf, len+1);
        return nn_values_nil();
    }
    s->data = buf;
    s->len = len;
    s->refc = 1;
    s->alloc = *alloc;

    return (nn_value) {.tag = NN_VALUE_STR, .string = s};
}

nn_value nn_values_array(nn_Alloc *alloc, nn_size_t len) {
    nn_array *arr = nn_alloc(alloc, sizeof(nn_array));
    if(arr == NULL) {
        return nn_values_nil();
    }
    arr->alloc = *alloc;
    arr->refc = 1;
    arr->len = len;
    nn_value *values = nn_alloc(alloc, sizeof(nn_value) * len);
    if(values == NULL) {
        nn_dealloc(alloc, arr, sizeof(nn_array));
        return nn_values_nil();
    }
    for(nn_size_t i = 0; i < len; i++) {
        values[i] = nn_values_nil();
    }
    arr->values = values;
    return (nn_value) {.tag = NN_VALUE_ARRAY, .array = arr};
}

nn_value nn_values_table(nn_Alloc *alloc, nn_size_t pairCount) {
    nn_table *table = nn_alloc(alloc, sizeof(nn_table));
    if(table == NULL) {
        return nn_values_nil();
    }
    table->alloc = *alloc;
    table->refc = 1;
    table->len = pairCount;
    nn_pair *pairs = nn_alloc(alloc, sizeof(nn_pair) * pairCount);
    if(pairs == NULL) {
        nn_dealloc(alloc, table, sizeof(nn_table));
        return nn_values_nil();
    }
    for(nn_size_t i = 0; i < pairCount; i++) {
        pairs[i].key = nn_values_nil();
        pairs[i].val = nn_values_nil();
    }
    table->pairs = pairs;
    return (nn_value) {.tag = NN_VALUE_TABLE, .table = table};
}

nn_value nn_values_resource(nn_size_t id) {
	return (nn_value) {
		.tag = NN_VALUE_RESOURCE,
		.resourceID = id,
	};
}

nn_size_t nn_values_getType(nn_value val) {
    return val.tag;
}

nn_value nn_values_retain(nn_value val) {
    if(val.tag == NN_VALUE_STR) {
        val.string->refc++;
    } else if(val.tag == NN_VALUE_ARRAY) {
        val.array->refc++;
    } else if(val.tag == NN_VALUE_TABLE) {
        val.table->refc++;
    }
    return val;
}

void nn_values_drop(nn_value val) {
    if(val.tag == NN_VALUE_STR) {
        val.string->refc--;
        if(val.string->refc == 0) {
            nn_Alloc *a = &val.string->alloc;
            nn_dealloc(a, val.string->data, val.string->len + 1);
            nn_dealloc(a, val.string, sizeof(nn_string));
        }
    } else if(val.tag == NN_VALUE_ARRAY) {
        val.array->refc--;
        if(val.array->refc == 0) {
            for(nn_size_t i = 0; i < val.array->len; i++) {
                nn_values_drop(val.array->values[i]);
            }
            nn_Alloc *a = &val.array->alloc;
            nn_dealloc(a, val.array->values, sizeof(nn_value) * val.array->len);
            nn_dealloc(a, val.array, sizeof(nn_array));
        }
    } else if(val.tag == NN_VALUE_TABLE) {
        val.table->refc--;
        if(val.table->refc == 0) {
            for(nn_size_t i = 0; i < val.table->len; i++) {
                nn_values_drop(val.table->pairs[i].key);
                nn_values_drop(val.table->pairs[i].val);
            }
            nn_Alloc *a = &val.table->alloc;
            nn_dealloc(a, val.table->pairs, sizeof(nn_pair) * val.table->len);
            nn_dealloc(a, val.table, sizeof(nn_table));
        }
    }
}

void nn_values_dropAll(nn_value *values, nn_size_t len) {
    for(nn_size_t i = 0; i < len; i++) {
        nn_values_drop(values[i]);
    }
}

void nn_values_set(nn_value arr, nn_size_t idx, nn_value val) {
    if(arr.tag != NN_VALUE_ARRAY) return;
    if(idx >= arr.array->len) return;
    nn_values_drop(arr.array->values[idx]);
    arr.array->values[idx] = val;
}

nn_value nn_values_get(nn_value arr, nn_size_t idx) {
    if(arr.tag != NN_VALUE_ARRAY) return nn_values_nil();
    if(idx >= arr.array->len) return nn_values_nil();
    return arr.array->values[idx];
}

void nn_values_setPair(nn_value obj, nn_size_t idx, nn_value key, nn_value val) {
    if(obj.tag != NN_VALUE_TABLE) return;
    if(idx >= obj.table->len) return;
    nn_values_drop(obj.table->pairs[idx].key);
    nn_values_drop(obj.table->pairs[idx].val);
    obj.table->pairs[idx].key = key;
    obj.table->pairs[idx].val = val;
}

nn_pair nn_values_getPair(nn_value obj, nn_size_t idx) {
    nn_pair badPair = {.key = nn_values_nil(), .val = nn_values_nil()};
    if(obj.tag != NN_VALUE_TABLE) return badPair;
    if(idx >= obj.table->len) return badPair;
    return obj.table->pairs[idx];
}

nn_integer_t nn_toInt(nn_value val) {
    if(val.tag == NN_VALUE_INT) return val.integer;
    if(val.tag == NN_VALUE_NUMBER) return val.number;
    return 0;
}

double nn_toNumber(nn_value val) {
    if(val.tag == NN_VALUE_INT) return val.integer;
    if(val.tag == NN_VALUE_NUMBER) return val.number;
    return 0;
}

nn_bool_t nn_toBoolean(nn_value val) {
    if(val.tag == NN_VALUE_NIL) return false;
    if(val.tag == NN_VALUE_BOOL) return val.boolean;
    return true;
}

const char *nn_toCString(nn_value val) {
    if(val.tag == NN_VALUE_CSTR) return val.cstring;
    if(val.tag == NN_VALUE_STR) return val.string->data;
    return NULL;
}

const char *nn_toString(nn_value val, nn_size_t *len) {
    nn_size_t l = 0;
    const char *c = NULL;

    if(val.tag == NN_VALUE_CSTR) {
        c = val.cstring;
        l = nn_strlen(c);
    }
    if(val.tag == NN_VALUE_STR) {
        c = val.string->data;
        l = val.string->len;
    }

    if(len != NULL) *len = l;
    return c;
}

nn_integer_t nn_toIntOr(nn_value val, nn_integer_t defaultVal) {
    if(val.tag == NN_VALUE_INT) return val.integer;
    if(val.tag == NN_VALUE_NUMBER) return val.number;
    return defaultVal;
}

double nn_toNumberOr(nn_value val, double defaultVal) {
    if(val.tag == NN_VALUE_INT) return val.integer;
    if(val.tag == NN_VALUE_NUMBER) return val.number;
    return defaultVal;
}

nn_bool_t nn_toBooleanOr(nn_value val, nn_bool_t defaultVal) {
    if(val.tag == NN_VALUE_BOOL) return val.boolean;
    return defaultVal;
}

nn_size_t nn_measurePacketSize(nn_value *vals, nn_size_t len) {
    nn_size_t size = 0;
    for(nn_size_t i = 0; i < len; i++) {
        nn_value val = vals[i];
        size += 2;
        if(val.tag == NN_VALUE_INT || val.tag == NN_VALUE_NUMBER) {
            size += 8;
        } else if(val.tag == NN_VALUE_STR) {
            nn_size_t len = val.string->len;
            if(len == 0) len = 1; // ask OC
            size += len;
        } else if(val.tag == NN_VALUE_CSTR) {
            nn_size_t len = nn_strlen(val.cstring);
            if(len == 0) len = 1; // ask OC
            size += len;
        } else if(val.tag == NN_VALUE_BOOL || val.tag == NN_VALUE_NIL) {
            size += 4;
        } else {
            // yeah no fuck off
            // we abuse 2's complement
            // TODO: NN_SIZE_MAX
            return -1;
        }
    }
    return size;
}
