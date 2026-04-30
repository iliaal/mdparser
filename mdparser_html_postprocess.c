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

#include <string.h>

/* ---------- heading entry list -------------------------------------- */

/* One entry per CMARK_NODE_HEADING in the document, in source order.
 * `slug` is the GitHub-style slug (possibly empty, possibly deduped).
 * `rendered` / `rendered_len` are cmark's standalone HTML rendering of
 * the heading node, used as a fingerprint to find the exact byte
 * position of THIS heading in the full document HTML. Without this
 * fingerprint, raw HTML blocks containing `<h1>` (only possible under
 * `unsafe: true`) would silently consume slugs intended for real
 * headings further down. `doc_offset` is filled in just before
 * apply_transforms by sequential memmem; SIZE_MAX means "not found"
 * and the entry is skipped during apply. */
typedef struct {
    char *slug;
    char *rendered;
    size_t rendered_len;
    size_t doc_offset;
    int level;
} mdparser_heading_entry;

typedef struct {
    mdparser_heading_entry *items;
    size_t count;
    size_t cap;
} mdparser_heading_list;

static bool heading_list_push(mdparser_heading_list *l, mdparser_heading_entry e)
{
    if (l->count == l->cap) {
        size_t new_cap = l->cap ? l->cap * 2 : 8;
        mdparser_heading_entry *next = erealloc(l->items, new_cap * sizeof(*next));
        if (!next) {
            return false;
        }
        l->items = next;
        l->cap = new_cap;
    }
    l->items[l->count++] = e;
    return true;
}

static void heading_list_free(mdparser_heading_list *l, cmark_mem *mem)
{
    for (size_t i = 0; i < l->count; i++) {
        if (l->items[i].slug) efree(l->items[i].slug);
        if (l->items[i].rendered) mem->free(l->items[i].rendered);
    }
    if (l->items) efree(l->items);
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

/* ---------- slugify -------------------------------------------------- */

/* GitHub-style slug:
 *   - lowercase ASCII A-Z -> a-z
 *   - keep ASCII alnum, '-', '_'
 *   - keep all bytes >= 0x80 verbatim (UTF-8 letters/digits survive)
 *   - replace any whitespace run with one '-'
 *   - drop other ASCII punctuation
 *   - collapse runs of '-', trim trailing '-'
 *
 * Output is heap-allocated via emalloc; caller owns. */
static char *mdparser_slugify(const char *text, size_t len)
{
    char *out = emalloc(len + 1);
    size_t o = 0;
    bool prev_dash = true;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];

        if (c >= 0x80) {
            /* Lowercasing non-ASCII without ICU is unsafe; pass through. */
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
    }

    while (o > 0 && out[o - 1] == '-') o--;
    out[o] = '\0';
    return out;
}

/* If `slug` already appears in `seen` (as the slug field of some prior
 * heading), append "-1", "-2", ... until a fresh value is found.
 * Empty slugs are not deduped: they suppress the id attribute entirely
 * at apply time, so deduping them would emit invalid id="-1". */
static char *mdparser_dedupe_slug(mdparser_heading_list *seen, char *slug)
{
    if (slug[0] == '\0') return slug;

    bool collides = false;
    for (size_t i = 0; i < seen->count; i++) {
        if (seen->items[i].slug && strcmp(seen->items[i].slug, slug) == 0) {
            collides = true;
            break;
        }
    }
    if (!collides) return slug;

    size_t base_len = strlen(slug);
    char *candidate = emalloc(base_len + 24);
    for (unsigned long n = 1; ; n++) {
        snprintf(candidate, base_len + 24, "%s-%lu", slug, n);
        bool taken = false;
        for (size_t i = 0; i < seen->count; i++) {
            if (seen->items[i].slug && strcmp(seen->items[i].slug, candidate) == 0) {
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

/* Append the text content of a heading subtree into `b`. Walks TEXT
 * and CODE leaves; recurses into emph/strong/link/image. Image children
 * (i.e. alt text) flow through naturally because cmark stores them as
 * TEXT children of the IMAGE node — that matches GitHub's slug
 * behavior, which includes alt text. softbreak/linebreak become a
 * single space. html_inline literals are skipped (we don't want raw
 * tag bytes leaking into the slug). Returns false only on depth
 * overflow; smart_str's allocator bails on OOM via the engine. */
static bool collect_heading_text(cmark_node *node, smart_str *b, int depth)
{
    if (depth > MDPARSER_MAX_AST_DEPTH) return false;

    cmark_node_type t = cmark_node_get_type(node);
    if (t == CMARK_NODE_TEXT || t == CMARK_NODE_CODE) {
        const char *lit = cmark_node_get_literal(node);
        if (lit) smart_str_appendl(b, lit, strlen(lit));
        return true;
    }
    if (t == CMARK_NODE_SOFTBREAK || t == CMARK_NODE_LINEBREAK) {
        smart_str_appendc(b, ' ');
        return true;
    }
    if (t == CMARK_NODE_HTML_INLINE) {
        return true;
    }

    for (cmark_node *child = cmark_node_first_child(node); child;
         child = cmark_node_next(child)) {
        if (!collect_heading_text(child, b, depth + 1)) return false;
    }
    return true;
}

/* ---------- heading list construction -------------------------------- */

/* Walk the document; for each heading node, capture its slug and a
 * standalone HTML rendering. The standalone rendering is later used
 * as a position fingerprint inside the full document HTML. Returns
 * false on alloc failure or depth overflow. `out` is left in a state
 * the caller can pass to heading_list_free regardless. */
static bool collect_headings(cmark_node *document, int cmark_options,
    cmark_llist *extensions, cmark_mem *mem, mdparser_heading_list *out)
{
    cmark_iter *iter = cmark_iter_new(document);
    if (!iter) return false;

    cmark_event_type ev;
    while ((ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        if (ev != CMARK_EVENT_ENTER) continue;
        cmark_node *cur = cmark_iter_get_node(iter);
        if (cmark_node_get_type(cur) != CMARK_NODE_HEADING) continue;

        smart_str tb = {0};
        if (!collect_heading_text(cur, &tb, 0)) {
            smart_str_free(&tb);
            cmark_iter_free(iter);
            return false;
        }

        const char *raw = tb.s ? ZSTR_VAL(tb.s) : "";
        size_t raw_len = tb.s ? ZSTR_LEN(tb.s) : 0;
        char *slug = mdparser_slugify(raw, raw_len);
        smart_str_free(&tb);
        slug = mdparser_dedupe_slug(out, slug);

        /* Standalone render of the heading. cmark allocates via `mem`;
         * we own the buffer and release it in heading_list_free. */
        char *rendered = cmark_render_html_with_mem(cur, cmark_options, extensions, mem);
        if (!rendered) {
            efree(slug);
            cmark_iter_free(iter);
            return false;
        }

        mdparser_heading_entry e = {
            .slug = slug,
            .rendered = rendered,
            .rendered_len = strlen(rendered),
            .doc_offset = SIZE_MAX,
            .level = cmark_node_get_heading_level(cur),
        };
        if (!heading_list_push(out, e)) {
            mem->free(rendered);
            efree(slug);
            cmark_iter_free(iter);
            return false;
        }
    }

    cmark_iter_free(iter);
    return true;
}

/* Resolve each heading entry's doc_offset by sequential memmem in the
 * full document HTML. Each search starts past the prior match to keep
 * source order. Entries whose rendered bytes can't be located keep
 * doc_offset=SIZE_MAX and are silently skipped at apply time; that
 * happens for headings whose body contains state-dependent renderer
 * output (e.g. footnote references inside a heading: the standalone
 * footnote_ix is 0 but the in-document index is whatever counter cmark
 * had reached at that point). */
static void resolve_heading_offsets(const char *html, size_t html_len,
    mdparser_heading_list *headings)
{
    size_t cursor = 0;
    for (size_t i = 0; i < headings->count; i++) {
        mdparser_heading_entry *e = &headings->items[i];
        if (cursor >= html_len) break;
        const char *hit = memmem(html + cursor, html_len - cursor,
            e->rendered, e->rendered_len);
        if (!hit) continue;
        e->doc_offset = (size_t)(hit - html);
        cursor = e->doc_offset + e->rendered_len;
    }
}

/* ---------- HTML transformation -------------------------------------- */

/* Find the next match of `<a href="`. cmark always emits this exact
 * sequence (double quotes, no leading whitespace) for every link tag
 * it generates, so a literal-byte scan is precise. */
static size_t find_anchor_open(const char *html, size_t from, size_t html_len)
{
    static const char needle[] = "<a href=\"";
    static const size_t nlen = sizeof(needle) - 1;
    if (from + nlen > html_len) return SIZE_MAX;
    const char *hit = memmem(html + from, html_len - from, needle, nlen);
    return hit ? (size_t)(hit - html) : SIZE_MAX;
}

/* Apply both postprocess transforms in one linear pass.
 *
 * Heading anchors: the anchored positions are pre-computed in
 * `headings->items[k].doc_offset` (zero-based byte offsets into
 * `html`). When we reach the next such offset we splice
 * ` id="slug"` between the level digit and the rest of the open tag.
 *
 * Nofollow: at every `<a href="` we emit `<a rel="..." href="...`.
 *
 * The two transforms target disjoint markup (`<h>` vs `<a>`) and
 * commute trivially, so a single scan handles both. */
static zend_string *apply_transforms(const char *html, size_t html_len,
    mdparser_heading_list *headings, int pp_mask)
{
    static const char rel_inject[] = "rel=\"nofollow noopener noreferrer\" ";
    static const size_t rel_inject_len = sizeof(rel_inject) - 1;

    smart_str out = {0};
    smart_str_alloc(&out, html_len + html_len / 4 + 64, 0);

    size_t i = 0;
    size_t heading_ix = 0;
    bool want_anchors = (pp_mask & MDPARSER_PP_HEADING_ANCHORS) != 0;
    bool want_nofollow = (pp_mask & MDPARSER_PP_NOFOLLOW_LINKS) != 0;

    /* Advance heading_ix past entries whose offsets failed to resolve
     * (rare; see resolve_heading_offsets). */
    if (want_anchors) {
        while (heading_ix < headings->count &&
               headings->items[heading_ix].doc_offset == SIZE_MAX) {
            heading_ix++;
        }
    }

    size_t next_anchor = want_nofollow ? find_anchor_open(html, 0, html_len) : SIZE_MAX;

    while (i < html_len) {
        if (want_anchors && heading_ix < headings->count &&
            i == headings->items[heading_ix].doc_offset)
        {
            mdparser_heading_entry *e = &headings->items[heading_ix];
            /* Emit "<hN" then optionally ` id="slug"`. The remainder of
             * the open tag (data-sourcepos="..." or just `>`) flows
             * naturally on the next loop iterations. Empty slug means
             * the heading slugified to nothing (pure punctuation): emit
             * <hN> with no id rather than id="". */
            smart_str_appendl(&out, html + i, 3);
            size_t s_len = strlen(e->slug);
            if (s_len) {
                smart_str_appendl(&out, " id=\"", 5);
                smart_str_appendl(&out, e->slug, s_len);
                smart_str_appendc(&out, '"');
            }
            i += 3;
            do {
                heading_ix++;
            } while (heading_ix < headings->count &&
                     headings->items[heading_ix].doc_offset == SIZE_MAX);
            continue;
        }

        if (want_nofollow && i == next_anchor) {
            /* Skip injection on in-document fragment anchors
             * (href="#..."). Footnote references and backrefs land
             * here. nofollow on an anchor that just jumps within the
             * same document is meaningless; leaving them alone keeps
             * the output cosmetically clean and avoids cluttering the
             * footnote-back-and-forth markup. */
            bool is_fragment = (i + 9 < html_len && html[i + 9] == '#');
            if (!is_fragment) {
                smart_str_appendl(&out, "<a ", 3);
                smart_str_appendl(&out, rel_inject, rel_inject_len);
                i += 3;
                next_anchor = find_anchor_open(html, i, html_len);
                continue;
            }
            /* Move the anchor cache past this fragment anchor so we do
             * not match it again on the next iteration; the byte at i
             * still falls through to the default per-byte emit. */
            next_anchor = find_anchor_open(html, i + 9, html_len);
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
    cmark_node *document, int cmark_options,
    cmark_llist *extensions, int pp_mask)
{
    if (pp_mask == 0) {
        return zend_string_init(html_in, html_len, 0);
    }

    mdparser_heading_list headings = {0};

    if (pp_mask & MDPARSER_PP_HEADING_ANCHORS) {
        if (!document) {
            /* Caller bug: anchors requested without an AST. */
            return NULL;
        }
        cmark_mem *mem = cmark_get_default_mem_allocator();
        if (!collect_headings(document, cmark_options, extensions, mem, &headings)) {
            heading_list_free(&headings, mem);
            return NULL;
        }
        resolve_heading_offsets(html_in, html_len, &headings);
    }

    zend_string *out = apply_transforms(html_in, html_len, &headings, pp_mask);
    cmark_mem *mem = cmark_get_default_mem_allocator();
    heading_list_free(&headings, mem);
    return out;
}
