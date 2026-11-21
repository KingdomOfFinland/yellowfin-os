#ifndef HTML_H
#define HTML_H

typedef void (*html_write_fn)(char c);
typedef void (*html_title_fn)(const char *title);

/*
 * Parse and render an HTML document.
 *
 * html:       Null-terminated HTML source
 * write:      Character output callback
 * title_out:  Optional page-title callback
 *
 * Returns 1 on success, 0 on invalid arguments.
 */

int html_render(
    const char *html,
    html_write_fn write,
    html_title_fn title_out
);

#endif
