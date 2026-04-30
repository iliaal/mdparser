/*
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2026 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,     |
  | that is bundled with this package in the file LICENSE, and is       |
  | available through the world-wide-web at the following url:          |
  | http://www.php.net/license/3_01.txt                                 |
  +----------------------------------------------------------------------+
  | Author: Ilia Alshanetsky <ilia@ilia.ws>                              |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "zend_smart_str.h"
#include "php_mdparser.h"
#include "mdparser_html_postprocess.h"

#include "cmark-gfm.h"

#include <ctype.h>
#include <string.h>

/* ---------- slug list ------------------------------------------------ */

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} mdparser_slug_list;

static void slug_list_init(mdparser_slug_list *l)
{
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

static bool slug_list_push(mdparser_slug_list *l, char *slug)
{
    if (l->count == l->cap) {
        size_t new_cap = l->cap ? l->cap * 2 : 8;
        char **next = erealloc(l->items, new_cap * sizeof(char *));
        if (!next) {
            return false;
        }
        l->items = next;
        l->cap = new_cap;
    }
    l->items[l->count++] = slug;
    return true;
}

static void slug_list_free(mdparser_slug_list *l)
{
    for (size_t i = 0; i < l->count; i++) {
        efree(l->items[i]);
    }
    if (l->items) {
        efree(l->items);
    }
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

/* ---------- slugify -------------------------------------------------- */

/* GitHub-style slug:
 *   - lowercase ASCII A-Z -> a-z
 *   - keep ASCII alnum, '-', '_'
 *   - keep all bytes >= 0x80 (UTF-8 continuation/lead) verbatim, so
 *     non-ASCII characters survive (CJK, Cyrillic, Greek, ...)
 *   - replace any run of whitespace with a single '-'
 *   - drop everything else (punctuation, symbols)
 *   - collapse runs of '-'
 *   - trim leading/trailing '-'
 *
 * Output is heap-allocated via emalloc; caller owns. Empty input returns
 * a zero-length string ("\0"-terminated). */
static char *mdparser_slugify(const char *text, size_t len)
{
    /* Worst-case output is len bytes (we only ever drop or replace,
     * never expand). +1 for terminator. */
    char *out = emalloc(len + 1);
    size_t o = 0;
    bool prev_dash = true; /* treat start as "after a dash" so leading
                              dashes are suppressed */

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];

        if (c >= 0x80) {
            /* UTF-8: keep verbatim. Lowercasing non-ASCII without ICU
             * is not safe, so leave case alone. */
            out[o++] = (char)c;
            prev_dash = false;
            continue;
        }

        if (c >= 'A' && c <= 'Z') {
            out[o++] = (char)(c + ('a' - 'A'));
            prev_dash = false;
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
            out[o++] = (char)c;
            prev_dash = false;
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '-') {
            if (!prev_dash) {
                out[o++] = '-';
                prev_dash = true;
            }
        }
        /* else: drop punctuation / symbols */
    }

    /* trim trailing dash */
    while (o > 0 && out[o - 1] == '-') {
        o--;
    }

    out[o] = '\0';
    return out;
}

/* If `slug` already appears in `seen`, append "-1", "-2", ... until a
 * fresh value is found. Frees the original `slug` on collision and
 * returns a fresh emalloc'd one; otherwise returns `slug` unchanged.
 * The returned pointer is always valid emalloc'd memory the caller
 * may push onto `seen`. Empty slugs collide too, mirroring GitHub's
 * "section" / "section-1" behavior for headings that slugify to "". */
static char *mdparser_dedupe_slug(mdparser_slug_list *seen, char *slug)
{
    /* Empty slugs never get an id="" emitted at apply-time, so don't
     * try to dedupe them into "-1" / "-2"; that would produce invalid
     * id="-1" on a heading whose plain text was empty after slugify. */
    if (slug[0] == '\0') {
        return slug;
    }

    bool collides = false;
    for (size_t i = 0; i < seen->count; i++) {
        if (strcmp(seen->items[i], slug) == 0) {
            collides = true;
            break;
        }
    }
    if (!collides) {
        return slug;
    }

    size_t base_len = strlen(slug);
    /* enough room for slug + "-" + 20-digit counter + NUL */
    char *candidate = emalloc(base_len + 24);
    for (unsigned long n = 1; ; n++) {
        snprintf(candidate, base_len + 24, "%s-%lu", slug, n);
        bool taken = false;
        for (size_t i = 0; i < seen->count; i++) {
            if (strcmp(seen->items[i], candidate) == 0) {
                taken = true;
                break;
            }
        }
        if (!taken) {
            efree(slug);
            return candidate;
        }
    }
}

/* ---------- heading text extraction ---------------------------------- */

/* Append the literal text of a heading subtree into `buf`. Walks
 * TEXT, CODE, and HTML_INLINE leaves; recurses into emph/strong/link.
 * Caller passes in a strbuf-like (cap=current allocation). Returns
 * false on alloc failure. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} mdparser_text_buf;

static bool tbuf_append(mdparser_text_buf *b, const char *src, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t need = b->len + n + 1;
        size_t new_cap = b->cap ? b->cap * 2 : 64;
        while (new_cap < need) new_cap *= 2;
        char *next = erealloc(b->data, new_cap);
        if (!next) {
            return false;
        }
        b->data = next;
        b->cap = new_cap;
    }
    if (n) {
        memcpy(b->data + b->len, src, n);
    }
    b->len += n;
    b->data[b->len] = '\0';
    return true;
}

static bool collect_heading_text(cmark_node *node, mdparser_text_buf *b, int depth)
{
    if (depth > MDPARSER_MAX_AST_DEPTH) {
        /* Mirrors the AST walker's depth cap. Headings shouldn't ever
         * nest this deeply, but cmark's AST is what it is. */
        return false;
    }

    cmark_node_type t = cmark_node_get_type(node);
    if (t == CMARK_NODE_TEXT || t == CMARK_NODE_CODE) {
        const char *lit = cmark_node_get_literal(node);
        if (lit) {
            return tbuf_append(b, lit, strlen(lit));
        }
        return true;
    }

    /* For other node kinds (emph, strong, link, image, softbreak,
     * linebreak, html_inline) recurse over children but don't include
     * their literal data: image alt text and html tags don't belong
     * in a slug. softbreak/linebreak become a space. */
    if (t == CMARK_NODE_SOFTBREAK || t == CMARK_NODE_LINEBREAK) {
        return tbuf_append(b, " ", 1);
    }

    for (cmark_node *child = cmark_node_first_child(node); child;
         child = cmark_node_next(child)) {
        if (!collect_heading_text(child, b, depth + 1)) {
            return false;
        }
    }
    return true;
}

/* ---------- heading list construction -------------------------------- */

/* Walk the document collecting (slug) entries in document order, one
 * per CMARK_NODE_HEADING. Slug list is appended to `out`. Returns
 * false on any allocation failure (out is left in caller's hands and
 * can be freed via slug_list_free). */
static bool collect_heading_slugs(cmark_node *document, mdparser_slug_list *out)
{
    cmark_iter *iter = cmark_iter_new(document);
    if (!iter) {
        return false;
    }

    cmark_event_type ev;
    while ((ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        if (ev != CMARK_EVENT_ENTER) {
            continue;
        }
        cmark_node *cur = cmark_iter_get_node(iter);
        if (cmark_node_get_type(cur) != CMARK_NODE_HEADING) {
            continue;
        }

        mdparser_text_buf tb = {0};
        if (!collect_heading_text(cur, &tb, 0)) {
            if (tb.data) efree(tb.data);
            cmark_iter_free(iter);
            return false;
        }

        char *slug = mdparser_slugify(tb.data ? tb.data : "", tb.len);
        if (tb.data) efree(tb.data);
        slug = mdparser_dedupe_slug(out, slug);
        if (!slug_list_push(out, slug)) {
            efree(slug);
            cmark_iter_free(iter);
            return false;
        }
    }

    cmark_iter_free(iter);
    return true;
}

/* ---------- HTML transformation -------------------------------------- */

/* Append n bytes from src to a smart_str. */
static inline void ss_append(smart_str *s, const char *src, size_t n)
{
    smart_str_appendl(s, src, n);
}

/* Find the next match of `<a href="`, treating it as a literal byte
 * pattern. cmark always emits this exact sequence (double quotes, no
 * leading whitespace) so a memmem-style scan is safe. Returns offset
 * within [from, html_len) or SIZE_MAX if not found. */
static size_t find_anchor_open(const char *html, size_t from, size_t html_len)
{
    static const char needle[] = "<a href=\"";
    static const size_t nlen = sizeof(needle) - 1;
    if (from + nlen > html_len) return SIZE_MAX;
    const char *hit = memmem(html + from, html_len - from, needle, nlen);
    return hit ? (size_t)(hit - html) : SIZE_MAX;
}

/* Detect if position `i` starts a heading open tag at line-start:
 *   - i == 0 or html[i-1] == '\n'
 *   - html[i..] begins with "<h[1-6]"
 *   - the byte after the digit is '>' or ' ' (so we don't match
 *     "<h7>" or "<head>" or "<hr>")
 * Returns the heading level (1..6) on match, 0 on no match. */
static int detect_heading_open(const char *html, size_t i, size_t html_len)
{
    if (i != 0 && html[i - 1] != '\n') return 0;
    if (i + 4 > html_len) return 0;
    if (html[i] != '<' || html[i + 1] != 'h') return 0;
    char d = html[i + 2];
    if (d < '1' || d > '6') return 0;
    char after = html[i + 3];
    if (after != '>' && after != ' ') return 0;
    return d - '0';
}

/* Apply the postprocess transforms in a single pass over the HTML.
 * Heading anchors get an `id="slug"` attribute injected immediately
 * after the level digit; nofollow gets `rel="nofollow noopener
 * noreferrer" ` injected before the existing `href="..."` of every
 * `<a href="..."` open tag. The two transforms commute (they target
 * disjoint markup), so a single linear scan handles both. */
static zend_string *apply_transforms(const char *html, size_t html_len,
    mdparser_slug_list *slugs, int pp_mask)
{
    static const char rel_inject[] = "rel=\"nofollow noopener noreferrer\" ";
    static const size_t rel_inject_len = sizeof(rel_inject) - 1;

    smart_str out = {0};
    /* Pre-grow: heading-id injections add ~10 + slug bytes; rel adds
     * ~36 bytes per link. A 2x size estimate is generous and avoids
     * many doublings on dense inputs. */
    smart_str_alloc(&out, html_len + html_len / 4 + 64, 0);

    size_t i = 0;
    size_t slug_ix = 0;
    bool want_anchors = (pp_mask & MDPARSER_PP_HEADING_ANCHORS) != 0;
    bool want_nofollow = (pp_mask & MDPARSER_PP_NOFOLLOW_LINKS) != 0;

    /* Cache the next anchor-open position so we don't re-scan the
     * whole tail on every byte. Updated when we cross past it. */
    size_t next_anchor = want_nofollow ? find_anchor_open(html, 0, html_len) : SIZE_MAX;

    while (i < html_len) {
        /* Heading injection */
        if (want_anchors && slug_ix < slugs->count) {
            int level = detect_heading_open(html, i, html_len);
            if (level) {
                /* Always consume one slug per heading even when the
                 * text slugified to nothing (so subsequent headings
                 * stay aligned). Skip the id="" emit for that case
                 * since empty id is invalid HTML5. */
                const char *s = slugs->items[slug_ix++];
                size_t s_len = strlen(s);
                ss_append(&out, html + i, 3); /* "<hN" */
                if (s_len) {
                    ss_append(&out, " id=\"", 5);
                    ss_append(&out, s, s_len);
                    smart_str_appendc(&out, '"');
                }
                i += 3;
                continue;
            }
        }

        /* Nofollow injection */
        if (want_nofollow && i == next_anchor) {
            /* Emit "<a " then "rel=...\" " then continue from "href" */
            ss_append(&out, "<a ", 3);
            ss_append(&out, rel_inject, rel_inject_len);
            i += 3; /* skip "<a " */
            next_anchor = find_anchor_open(html, i, html_len);
            continue;
        }

        smart_str_appendc(&out, html[i]);
        i++;
    }

    smart_str_0(&out);
    return out.s ? out.s : ZSTR_EMPTY_ALLOC();
}

/* ---------- public entry point --------------------------------------- */

zend_string *mdparser_html_postprocess(
    const char *html_in, size_t html_len,
    cmark_node *document, int pp_mask)
{
    if (pp_mask == 0) {
        return zend_string_init(html_in, html_len, 0);
    }

    mdparser_slug_list slugs;
    slug_list_init(&slugs);

    if (pp_mask & MDPARSER_PP_HEADING_ANCHORS) {
        if (!document) {
            /* Caller bug: anchors requested without an AST. */
            return NULL;
        }
        if (!collect_heading_slugs(document, &slugs)) {
            slug_list_free(&slugs);
            return NULL;
        }
    }

    zend_string *out = apply_transforms(html_in, html_len, &slugs, pp_mask);
    slug_list_free(&slugs);
    return out;
}
