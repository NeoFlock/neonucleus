#include "neonucleus.h"
#include <string.h>

nn_value nn_values_nil() {
    return (nn_value) {.tag = NN_VALUE_NIL};
}

nn_value nn_values_integer(intptr_t integer) {
    return (nn_value) {.tag = NN_VALUE_INT, .integer = integer};
}

nn_value nn_values_number(double num) {
    return (nn_value) {.tag = NN_VALUE_NUMBER, .number = num};
}

nn_value nn_values_boolean(bool boolean) {
    return (nn_value) {.tag = NN_VALUE_BOOL, .boolean = boolean};
}

nn_value nn_values_cstring(const char *string) {
    return (nn_value) {.tag = NN_VALUE_CSTR, .cstring = string};
}

nn_value nn_values_string(const char *string, size_t len) {
    if(len == 0) len = strlen(string);

    char *buf = nn_malloc(len+1);
    if(buf == NULL) {
        return nn_values_nil();
    }
    memcpy(buf, string, len);
    buf[len] = '\0';

    nn_string *s = nn_malloc(sizeof(nn_string));
    if(s == NULL) {
        nn_free(buf);
        return nn_values_nil();
    }
    s->data = buf;
    s->len = len;
    s->refc = 1;

    return (nn_value) {.tag = NN_VALUE_STR, .string = s};
}

nn_value nn_values_array(size_t len) {
    nn_array *arr = nn_malloc(sizeof(nn_array));
    if(arr == NULL) {
        return nn_values_nil();
    }
    arr->refc = 1;
    arr->len = len;
    nn_value *values = nn_malloc(sizeof(nn_value) * len);
    if(values == NULL) {
        nn_free(arr);
        return nn_values_nil();
    }
    for(size_t i = 0; i < len; i++) {
        values[i] = nn_values_nil();
    }
    arr->values = values;
    return (nn_value) {.tag = NN_VALUE_ARRAY, .array = arr};
}

nn_value nn_values_table(size_t pairCount) {
    nn_table *table = nn_malloc(sizeof(nn_table));
    if(table == NULL) {
        return nn_values_nil();
    }
    table->refc = 1;
    table->len = pairCount;
    nn_pair *pairs = nn_malloc(sizeof(nn_pair) * pairCount);
    if(pairs == NULL) {
        nn_free(table);
        return nn_values_nil();
    }
    for(size_t i = 0; i < pairCount; i++) {
        pairs[i].key = nn_values_nil();
        pairs[i].val = nn_values_nil();
    }
    table->pairs = pairs;
    return (nn_value) {.tag = NN_VALUE_TABLE, .table = table};
}

size_t nn_values_getType(nn_value val) {
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
            nn_free(val.string->data);
            nn_free(val.string);
        }
    } else if(val.tag == NN_VALUE_ARRAY) {
        val.array->refc--;
        if(val.array->refc == 0) {
            for(size_t i = 0; i < val.array->len; i++) {
                nn_values_drop(val.array->values[i]);
            }
            nn_free(val.array->values);
            nn_free(val.array);
        }
    } else if(val.tag == NN_VALUE_TABLE) {
        val.table->refc--;
        if(val.table->refc == 0) {
            for(size_t i = 0; i < val.table->len; i++) {
                nn_values_drop(val.table->pairs[i].key);
                nn_values_drop(val.table->pairs[i].val);
            }
            nn_free(val.table->pairs);
            nn_free(val.table);
        }
    }
}

void nn_values_set(nn_value arr, size_t idx, nn_value val) {
    if(arr.tag != NN_VALUE_ARRAY) return;
    if(idx >= arr.array->len) return;
    nn_values_drop(arr.array->values[idx]);
    arr.array->values[idx] = val;
}

nn_value nn_values_get(nn_value arr, size_t idx) {
    if(arr.tag != NN_VALUE_ARRAY) return nn_values_nil();
    if(idx >= arr.array->len) return nn_values_nil();
    return arr.array->values[idx];
}

void nn_values_setPair(nn_value obj, size_t idx, nn_value key, nn_value val) {
    if(obj.tag != NN_VALUE_TABLE) return;
    if(idx >= obj.table->len) return;
    nn_values_drop(obj.table->pairs[idx].key);
    nn_values_drop(obj.table->pairs[idx].val);
    obj.table->pairs[idx].key = key;
    obj.table->pairs[idx].val = val;
}

nn_pair nn_values_getPair(nn_value obj, size_t idx) {
    nn_pair badPair = {.key = nn_values_nil(), .val = nn_values_nil()};
    if(obj.tag != NN_VALUE_TABLE) return badPair;
    if(idx >= obj.table->len) return badPair;
    return obj.table->pairs[idx];
}

intptr_t nn_toInt(nn_value val) {
    if(val.tag == NN_VALUE_INT) return val.integer;
    if(val.tag == NN_VALUE_NUMBER) return val.number;
    return 0;
}

double nn_toNumber(nn_value val) {
    if(val.tag == NN_VALUE_INT) return val.integer;
    if(val.tag == NN_VALUE_NUMBER) return val.number;
    return 0;
}

bool nn_toBoolean(nn_value val) {
    if(val.tag == NN_VALUE_NIL) return false;
    if(val.tag == NN_VALUE_BOOL) return val.boolean;
    return true;
}

const char *nn_toCString(nn_value val) {
    if(val.tag == NN_VALUE_CSTR) return val.cstring;
    if(val.tag == NN_VALUE_STR) return val.string->data;
    return NULL;
}

const char *nn_toString(nn_value val, size_t *len) {
    size_t l = 0;
    const char *c = NULL;

    if(val.tag == NN_VALUE_CSTR) {
        c = val.cstring;
        l = strlen(c);
    }
    if(val.tag == NN_VALUE_STR) {
        c = val.string->data;
        l = val.string->len;
    }

    if(len != NULL) *len = l;
    return c;
}

size_t nn_measurePacketSize(nn_value *vals, size_t len) {
    size_t size = 0;
    for(size_t i = 0; i < len; i++) {
        nn_value val = vals[i];
        size += 2;
        if(val.tag == NN_VALUE_INT || val.tag == NN_VALUE_NUMBER) {
            size += 8;
        } else if(val.tag == NN_VALUE_STR) {
            size_t len = val.string->len;
            if(len == 0) len = 1; // ask OC
            size += len;
        } else if(val.tag == NN_VALUE_CSTR) {
            size_t len = strlen(val.cstring);
            if(len == 0) len = 1; // ask OC
            size += len;
        } else if(val.tag == NN_VALUE_BOOL || val.tag == NN_VALUE_NIL) {
            size += 4;
        } else {
            // yeah no fuck off
            return SIZE_MAX;
        }
    }
    return size;
}
