/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <limits.h>
#include <stdio.h>
#include "../src/pageinput.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { failures++; \
    fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main(void)
{
    CHECK(parse_page_input("1", 128) == 0);
    CHECK(parse_page_input("128", 128) == 127);
    CHECK(parse_page_input("  002 \t", 128) == 1);
    CHECK(parse_page_input("2147483647", INT_MAX) == INT_MAX - 1);
    CHECK(parse_page_input("0", 128) == -1);
    CHECK(parse_page_input("129", 128) == -1);
    CHECK(parse_page_input("-1", 128) == -1);
    CHECK(parse_page_input("2abc", 128) == -1);
    CHECK(parse_page_input("1 2", 128) == -1);
    CHECK(parse_page_input("99999999999999999999", INT_MAX) == -1);
    CHECK(parse_page_input("", 128) == -1);
    CHECK(parse_page_input("  ", 128) == -1);
    CHECK(parse_page_input(NULL, 128) == -1);
    CHECK(parse_page_input("1", 0) == -1);
    if (failures) return 1;
    puts("pageinput_test: all checks passed");
    return 0;
}
