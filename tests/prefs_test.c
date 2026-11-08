/* Host test for src/prefs.h: the preferences file format. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/prefs.h"

static void read_text(struct Prefs *p, const char *text)
{
    FILE *f = tmpfile();
    assert(f);
    fputs(text, f);
    rewind(f);
    prefs_read(p, f);
    fclose(f);
}

static void write_text(const struct Prefs *p, char *out, size_t size)
{
    FILE *f = tmpfile();
    size_t n;
    assert(f);
    prefs_write(p, f);
    rewind(f);
    n = fread(out, 1, size - 1, f);
    out[n] = 0;
    fclose(f);
}

int main(void)
{
    struct Prefs p;
    char text[256];

    /* Defaults: the panel is shown. */
    prefs_defaults(&p);
    assert(p.panel_shown == 1);

    /* A hidden panel is read back; the header and blanks are skipped. */
    prefs_defaults(&p);
    read_text(&p, "folio-prefs 1\n\npanel hidden\n");
    assert(p.panel_shown == 0);

    /* Tabs, trailing spaces and CRLF line ends. */
    prefs_defaults(&p);
    read_text(&p, "folio-prefs 1\r\n\tpanel\t shown  \r\n");
    assert(p.panel_shown == 1);

    /* A value that makes no sense keeps the setting; unknown keys and a
     * key without a value are ignored. */
    prefs_defaults(&p);
    p.panel_shown = 0;
    read_text(&p, "panel sideways\nfuture-key 42\npanel\n# comment\n");
    assert(p.panel_shown == 0);

    /* The last line wins. */
    prefs_defaults(&p);
    read_text(&p, "panel hidden\npanel shown\n");
    assert(p.panel_shown == 1);

    /* Writing and reading back round-trips both states. */
    p.panel_shown = 0;
    write_text(&p, text, sizeof(text));
    assert(!strncmp(text, PREFS_HEADER "\n", strlen(PREFS_HEADER) + 1));
    assert(strstr(text, "panel hidden\n"));
    prefs_defaults(&p);
    read_text(&p, text);
    assert(p.panel_shown == 0);
    p.panel_shown = 1;
    write_text(&p, text, sizeof(text));
    assert(strstr(text, "panel shown\n"));

    /* An empty file leaves the defaults. */
    prefs_defaults(&p);
    read_text(&p, "");
    assert(p.panel_shown == 1);

    puts("prefs_test: ok");
    return 0;
}
