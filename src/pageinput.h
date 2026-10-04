/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* One-based page entry converted to the reader's zero-based page index. */
#ifndef FOLIO_PAGEINPUT_H
#define FOLIO_PAGEINPUT_H
#include <ctype.h>

static int parse_page_input(const char *text, int count)
{
    int value = 0;
    if (!text || count < 1) return -1;
    while (isspace((unsigned char)*text)) text++;
    if (*text < '0' || *text > '9') return -1;
    while (*text >= '0' && *text <= '9')
    {
        int digit = *text++ - '0';
        /* Bound before multiplying, including on 32-bit hosts. */
        if (value > count / 10 || (value == count / 10 && digit > count % 10))
            return -1;
        value = value * 10 + digit;
    }
    while (isspace((unsigned char)*text)) text++;
    return !*text && value > 0 ? value - 1 : -1;
}

#endif
