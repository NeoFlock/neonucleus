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
