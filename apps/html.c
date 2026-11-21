/*
 * html.c - Basic HTML parser and text renderer
 * YellowfinOS
 *
 * Stage 1: HTML parsing
 */

#include "html.h"

#define HTML_TITLE_MAX 128

static int html_is_space(char c)
{
    return c == ' ' || c == '\t' ||
           c == '\n' || c == '\r';
}

static char html_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');

    return c;
}

static int html_name_is(
    const char *start,
    const char *end,
    const char *name
)
{
    while (start < end && *name) {
        if (html_lower(*start) != *name)
            return 0;

        start++;
        name++;
    }

    return start == end && *name == 0;
}

static void html_puts(
    html_write_fn write,
    const char *s
)
{
    while (*s)
        write(*s++);
}

static void html_newline(html_write_fn write)
{
    write('\n');
    write('\n');
}

static int html_entity(
    const char *s,
    int len,
    char *out
)
{
    if (len == 3 && s[0] == 'a' &&
        s[1] == 'm' && s[2] == 'p') {
        *out = '&';
        return 1;
    }

    if (len == 2 && s[0] == 'l' &&
        s[1] == 't') {
        *out = '<';
        return 1;
    }

    if (len == 2 && s[0] == 'g' &&
        s[1] == 't') {
        *out = '>';
        return 1;
    }

    if (len == 4 && s[0] == 'q' &&
        s[1] == 'u' && s[2] == 'o' &&
        s[3] == 't') {
        *out = '"';
        return 1;
    }

    if (len == 4 && s[0] == 'a' &&
        s[1] == 'p' && s[2] == 'o' &&
        s[3] == 's') {
        *out = '\'';
        return 1;
    }

    if (len == 4 && s[0] == 'n' &&
        s[1] == 'b' && s[2] == 's' &&
        s[3] == 'p') {
        *out = ' ';
        return 1;
    }

    return 0;
}

int html_render(
    const char *html,
    html_write_fn write,
    html_title_fn title_out
)
{
    const char *p;
    const char *tag_start;
    const char *tag_end;

    char title[HTML_TITLE_MAX];
    int title_len = 0;

    int in_title = 0;
    int in_head = 0;
    int in_script = 0;
    int in_style = 0;
    int closing;
    int self_closing;

    int pending_space = 0;
    int line_has_text = 0;

    if (!html || !write)
        return 0;

    p = html;

    title[0] = 0;

    while (*p) {

        /* HTML tag */
        if (*p == '<') {

            tag_start = ++p;

            /* HTML comment */
            if (*p == '!' &&
                p[1] == '-' && p[2] == '-') {

                p += 3;

                while (*p) {
                    if (p[0] == '-' &&
                        p[1] == '-' &&
                        p[2] == '>') {
                        p += 3;
                        break;
                    }

                    p++;
                }

                continue;
            }

            closing = 0;

            if (*p == '/') {
                closing = 1;
                p++;
            }

            while (*p &&
                   html_is_space(*p))
                p++;

            tag_start = p;

            while (*p &&
                   *p != '>' &&
                   !html_is_space(*p) &&
                   *p != '/') {
                p++;
            }

            tag_end = p;

            self_closing = 0;

            while (*p && *p != '>') {
                if (*p == '/')
                    self_closing = 1;

                p++;
            }

            if (*p == '>')
                p++;

            /* Head */
            if (html_name_is(tag_start, tag_end, "head")) {
                in_head = !closing;
                continue;
            }

            /* Page title */
            if (html_name_is(tag_start, tag_end, "title")) {

                if (closing) {
                    in_title = 0;
                } else {
                    in_title = 1;
                }

                continue;
            }

            /* Ignore scripts */
            if (html_name_is(tag_start, tag_end, "script")) {
                in_script = !closing && !self_closing;
                continue;
            }

            /* Ignore stylesheets */
            if (html_name_is(tag_start, tag_end, "style")) {
                in_style = !closing && !self_closing;
                continue;
            }

            if (in_script || in_style || in_head)
                continue;

            /* Headings */
            if (html_name_is(tag_start, tag_end, "h1") ||
                html_name_is(tag_start, tag_end, "h2") ||
                html_name_is(tag_start, tag_end, "h3")) {

                if (!closing) {
                    html_newline(write);
                    html_puts(write, "## ");
                    line_has_text = 0;
                } else {
                    html_newline(write);
                    line_has_text = 0;
                }

                continue;
            }

            /* Paragraphs */
            if (html_name_is(tag_start, tag_end, "p")) {
                if (!closing) {
                    if (line_has_text)
                        html_newline(write);
                } else {
                    html_newline(write);
                }

                line_has_text = 0;
                pending_space = 0;
                continue;
            }

            /* Line breaks */
            if (html_name_is(tag_start, tag_end, "br")) {
                html_newline(write);
                line_has_text = 0;
                continue;
            }

            /* Lists */
            if (html_name_is(tag_start, tag_end, "li")) {
                if (!closing) {
                    html_newline(write);
                    html_puts(write, "  * ");
                    line_has_text = 0;
                } else {
                    html_newline(write);
                    line_has_text = 0;
                }

                continue;
            }

            /* Horizontal rule */
            if (html_name_is(tag_start, tag_end, "hr")) {
                html_newline(write);
                html_puts(write, "--------------------------------");
                html_newline(write);
                line_has_text = 0;
                continue;
            }

            /* Ignore other tags */
            continue;
        }

        /* Ignore script/style content */
        if (in_script || in_style) {
            p++;
            continue;
        }

        /* Extract page title */
        if (in_title) {

            if (title_len < HTML_TITLE_MAX - 1) {
                title[title_len++] = *p;
                title[title_len] = 0;
            }

            p++;
            continue;
        }

        /* Ignore head content */
        if (in_head) {
            p++;
            continue;
        }

        /* HTML entities */
        if (*p == '&') {
            const char *entity = ++p;
            char decoded;
            int len = 0;

            while (*p && *p != ';' &&
                   len < 8) {
                p++;
                len++;
            }

            if (*p == ';') {
                p++;

                if (html_entity(entity, len, &decoded)) {
                    write(decoded);
                    line_has_text = 1;
                    continue;
                }
            }

            write('&');
            line_has_text = 1;
            continue;
        }

        /* Whitespace collapsing */
        if (html_is_space(*p)) {
            pending_space = 1;
            p++;
            continue;
        }

        if (pending_space && line_has_text)
            write(' ');

        pending_space = 0;

        write(*p++);
        line_has_text = 1;
    }

    /* Report page title */
    if (title_out && title_len > 0)
        title_out(title);

    html_newline(write);

    return 1;
}
