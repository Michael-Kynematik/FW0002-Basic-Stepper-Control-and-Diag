#include "json_helpers.h"

#include <stdio.h>

static void json_print_escaped_char(unsigned char c)
{
    switch (c)
    {
    case '\\':
        fputs("\\\\", stdout);
        break;
    case '"':
        fputs("\\\"", stdout);
        break;
    case '\n':
        fputs("\\n", stdout);
        break;
    case '\r':
        fputs("\\r", stdout);
        break;
    case '\t':
        fputs("\\t", stdout);
        break;
    default:
        if (c < 0x20)
        {
            static const char hex[] = "0123456789ABCDEF";
            fputs("\\u00", stdout);
            fputc(hex[(c >> 4) & 0x0F], stdout);
            fputc(hex[c & 0x0F], stdout);
        }
        else
        {
            fputc(c, stdout);
        }
        break;
    }
}

void json_print_escaped_string(const char *s)
{
    if (s == NULL)
    {
        fputs("null", stdout);
        return;
    }

    fputc('"', stdout);
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; ++p)
    {
        json_print_escaped_char(*p);
    }
    fputc('"', stdout);
}

bool json_escape_to_buf(const char *in, char *out, size_t out_len)
{
    if (in == NULL || out == NULL || out_len == 0)
    {
        return false;
    }

    size_t pos = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p != '\0'; ++p)
    {
        unsigned char c = *p;
        const char *esc = NULL;
        size_t needed = 0;

        switch (c)
        {
        case '\\':
            esc = "\\\\";
            needed = 2;
            break;
        case '"':
            esc = "\\\"";
            needed = 2;
            break;
        case '\n':
            esc = "\\n";
            needed = 2;
            break;
        case '\r':
            esc = "\\r";
            needed = 2;
            break;
        case '\t':
            esc = "\\t";
            needed = 2;
            break;
        default:
            if (c < 0x20)
            {
                needed = 6;
            }
            else
            {
                needed = 1;
            }
            break;
        }

        if (pos + needed >= out_len)
        {
            out[0] = '\0';
            return false;
        }

        if (esc != NULL)
        {
            out[pos++] = esc[0];
            out[pos++] = esc[1];
            continue;
        }

        if (c < 0x20)
        {
            static const char hex[] = "0123456789ABCDEF";
            out[pos++] = '\\';
            out[pos++] = 'u';
            out[pos++] = '0';
            out[pos++] = '0';
            out[pos++] = hex[(c >> 4) & 0x0F];
            out[pos++] = hex[c & 0x0F];
        }
        else
        {
            out[pos++] = (char)c;
        }
    }

    out[pos] = '\0';
    return true;
}
