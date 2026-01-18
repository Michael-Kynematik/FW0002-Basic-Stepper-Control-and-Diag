#ifndef JSON_HELPERS_H
#define JSON_HELPERS_H

#include <stdbool.h>
#include <stddef.h>

void json_print_escaped_string(const char *s);
bool json_escape_to_buf(const char *in, char *out, size_t out_len);

#endif
