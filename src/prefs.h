/* Application preferences: the text format of ENV:Folio/prefs.
 *
 * One `key value` pair per line after a `folio-prefs 1` header. Unknown
 * keys are ignored on reading and not written back. The reading state of documents is a different file (ENV:Folio/state) with
 * its own format; nothing here touches it.
 *
 * No AROS or MUI dependencies: tests/prefs_test.c drives this on the host.
 * folio.c does the file I/O and applies the values. */

#ifndef FOLIO_PREFS_H
#define FOLIO_PREFS_H

#include <string.h>
#include <stdio.h>

#define PREFS_HEADER "folio-prefs 1"
#define PREFS_KEY_PANEL "panel"          /* shown | hidden */

struct Prefs
{
    int panel_shown;        /* the Pages/Outline panel; shown by default */
};

static void prefs_defaults(struct Prefs *p)
{
    p->panel_shown = 1;
}

/* Split `line` (modified in place) into key and value, both without
 * surrounding spaces, tabs or line ends; returns 0 for the header, blank
 * and comment lines, and lines without a value. */
static int prefs_split(char *line, char **key, char **value)
{
    char *k, *v;
    line[strcspn(line, "\r\n")] = 0;
    k = line + strspn(line, " \t");
    if (!*k || *k == '#' || !strncmp(k, PREFS_HEADER, strlen(PREFS_HEADER)))
        return 0;
    v = k + strcspn(k, " \t");
    if (!*v)
        return 0;
    *v++ = 0;
    v += strspn(v, " \t");
    {
        size_t n = strlen(v);
        while (n && (v[n - 1] == ' ' || v[n - 1] == '\t'))
            v[--n] = 0;
    }
    if (!*v)
        return 0;
    *key = k;
    *value = v;
    return 1;
}

/* Apply one `key value` pair. Returns 1 when the key was known. A known key
 * with a value that makes no sense keeps the previous setting. */
static int prefs_apply(struct Prefs *p, const char *key, const char *value)
{
    if (!strcmp(key, PREFS_KEY_PANEL))
    {
        if (!strcmp(value, "shown")) p->panel_shown = 1;
        else if (!strcmp(value, "hidden")) p->panel_shown = 0;
        return 1;
    }
    return 0;
}

/* Read the whole file: every line through prefs_apply(). */
static void prefs_read(struct Prefs *p, FILE *f)
{
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char *k, *v;
        if (prefs_split(line, &k, &v))
            prefs_apply(p, k, v);
    }
}

/* Write the header and every setting. */
static void prefs_write(const struct Prefs *p, FILE *f)
{
    fprintf(f, "%s\n", PREFS_HEADER);
    fprintf(f, "%s %s\n", PREFS_KEY_PANEL, p->panel_shown ? "shown" : "hidden");
}

#endif
