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

/* memmem is a GNU/BSD extension; MSVC's libc does not provide it. Use a
 * portable two-loop fallback on Windows. The needle lengths we pass are
 * either short literals ("<a href=\"", "</script>"...) or a single
 * heading's rendered HTML, so a naive scan is acceptable. */
#ifdef _WIN32
static const void *mdparser_memmem(const void *haystack, size_t haystack_len,
    const void *needle, size_t needle_len)
{
    if (needle_len == 0) return haystack;
    if (needle_len > haystack_len) return NULL;
    const char *h = (const char *)haystack;
    const char *n = (const char *)needle;
    const char *end = h + (haystack_len - needle_len) + 1;
    char first = n[0];
    for (const char *p = h; p < end; p++) {
        const char *cand = (const char *)memchr(p, first, (size_t)(end - p));
        if (!cand) return NULL;
        if (memcmp(cand, n, needle_len) == 0) return cand;
        p = cand;
    }
    return NULL;
}
#define memmem mdparser_memmem
#endif

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
 * and the entry is skipped during apply.
 *
 * KNOWN LIMITATION (review CR-003): the byte-fingerprint approach
 * cannot distinguish a Markdown heading from a raw HTML block whose
 * rendered bytes match exactly. resolve_heading_offsets now skips
 * over scan_skip_region territory (comments, CDATA, script/style/
 * title/textarea/iframe/noscript/xmp/noembed/noframes/plaintext
 * bodies), so fingerprint matches inside those regions cannot hijack
 * a slug. But under `unsafe:true, tagfilter:false`, input like
 * `<h1>same</h1>\n\n# same\n` produces two `<h1>same</h1>` substrings
 * outside any skip region, and the first (raw HTML) match wins. The
 * real Markdown heading is left without an id. A durable fix needs
 * renderer-level heading-id support so node identity carries into
 * output. Until then, callers in unsafe mode should not rely on
 * heading-id stability when raw HTML headings can collide with real
 * ones. Regression test: tests/030_anchor_unsafe_collision.phpt. */
typedef struct {
    char *slug;
    char *rendered;
    size_t rendered_len;
    size_t doc_offset;
} mdparser_heading_entry;

typedef struct {
    mdparser_heading_entry *items;
    size_t count;
    size_t cap;
    /* Index of slug strings already used, for O(1) collision detection
     * during dedupe. NULL until the first push; lazily allocated. */
    HashTable *slug_index;
} mdparser_heading_list;

/* Dedupe retry cap: after this many "-N" attempts on the same base
 * slug, fall back to emitting a heading without an id rather than
 * spinning the loop. The hash-index path makes this almost unreachable,
 * but the cap also bounds the snprintf+lookup work even on a malformed
 * index. */
#define MDPARSER_DEDUPE_MAX_RETRIES 100000

static bool heading_list_push(mdparser_heading_list *l, mdparser_heading_entry e)
{
    if (l->count == l->cap) {
        size_t new_cap = l->cap ? l->cap * 2 : 8;
        mdparser_heading_entry *next = safe_erealloc(l->items, sizeof(*next), new_cap, 0);
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
    if (l->slug_index) {
        zend_hash_destroy(l->slug_index);
        FREE_HASHTABLE(l->slug_index);
        l->slug_index = NULL;
    }
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

/* Record `slug` in the dedupe index. Caller still owns the slug pointer;
 * the index just stores a presence flag keyed by the slug bytes. */
static void heading_list_index_slug(mdparser_heading_list *l, const char *slug, size_t slug_len)
{
    if (!slug || slug_len == 0) return;
    if (!l->slug_index) {
        ALLOC_HASHTABLE(l->slug_index);
        zend_hash_init(l->slug_index, 16, NULL, ZVAL_PTR_DTOR, 0);
    }
    zval marker;
    ZVAL_TRUE(&marker);
    zend_hash_str_add(l->slug_index, slug, slug_len, &marker);
}

static bool heading_list_slug_taken(mdparser_heading_list *l, const char *slug, size_t slug_len)
{
    if (!l->slug_index) return false;
    return zend_hash_str_exists(l->slug_index, slug, slug_len);
}

/* HTML5 "ASCII whitespace" per WHATWG § 4.6: U+0009 TAB, U+000A LF,
 * U+000C FF, U+000D CR, U+0020 SPACE. Used for tag-name delimiters
 * and close-tag whitespace tolerance; missing U+000C lets a `\f`
 * before / after a tag fool the scanner into treating the tag as
 * closed (or unclosed). https://html.spec.whatwg.org/dev/syntax.html */
static inline bool mdparser_is_html_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* Locale-independent ASCII-only case-insensitive byte compare. strncasecmp
 * honours LC_CTYPE; in tr_TR.UTF-8 the dotless/dotted-I rule makes
 * 'I' != 'i'. We only ever compare against the literal HTML tag names
 * "script"/"style" and their close forms, so an ASCII fold is sufficient
 * and safe under any locale. */
static int mdparser_ascii_strncasecmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char x = (unsigned char)a[i];
        unsigned char y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + ('a' - 'A'));
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + ('a' - 'A'));
        if (x != y) return (int)x - (int)y;
    }
    return 0;
}

/* ---------- slugify -------------------------------------------------- */

/* GitHub-style slug:
 *   - lowercase ASCII A-Z -> a-z
 *   - keep ASCII alnum, '-', '_'
 *   - valid UTF-8 multi-byte sequences (0xC2..0xF4 leads + the right
 *     number of 0x80..0xBF continuations) pass through verbatim, so
 *     `id="日本語"` matches GitHub's behavior and modern browsers
 *     handle it natively.
 *   - invalid leading bytes (0x80..0xBF lone continuation, 0xC0/0xC1
 *     overlong, 0xF5..0xFF, or any truncated sequence) are percent-
 *     encoded (RFC 3986 fragment encoding). Without this, validateUtf8:false
 *     callers can land malformed UTF-8 in id="..." attributes; browsers
 *     handle invalid fragments inconsistently.
 *   - replace any whitespace run with one '-'
 *   - drop other ASCII punctuation
 *   - collapse runs of '-', trim trailing '-'
 *
 * Output is heap-allocated via emalloc; caller owns. Worst-case
 * expansion is 3x for all-invalid-byte input. */
static char *mdparser_slugify(const char *text, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    char *out = emalloc(len * 3 + 1);
    size_t o = 0;
    bool prev_dash = true;

    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)text[i];

        if (c < 0x80) {
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
            i++;
            continue;
        }

        /* Multi-byte UTF-8 leading byte. Determine expected length and
         * verify all bytes against RFC 3629:
         *   - lead byte in valid class (rejects 0xC0/0xC1, 0xF5..0xFF
         *     and lone 0x80..0xBF outright);
         *   - second byte tightened per-lead to reject overlong forms
         *     (0xE0 + 0x80..0x9F = overlong 3-byte; 0xF0 + 0x80..0x8F
         *     = overlong 4-byte), UTF-16 surrogates (0xED + 0xA0..0xBF
         *     encodes U+D800..U+DFFF), and out-of-Unicode codepoints
         *     (0xF4 + 0x90..0xBF encodes > U+10FFFF);
         *   - remaining continuation bytes must be 0x80..0xBF.
         * Anything else is percent-encoded one byte at a time so the
         * id="..." attribute holds only valid UTF-8 representable as
         * RFC 3986 fragment bytes. */
        size_t expect = 0;
        unsigned char min2 = 0x80, max2 = 0xBF;
        if (c >= 0xC2 && c <= 0xDF)      expect = 2;
        else if (c == 0xE0)              { expect = 3; min2 = 0xA0; }
        else if (c >= 0xE1 && c <= 0xEC) expect = 3;
        else if (c == 0xED)              { expect = 3; max2 = 0x9F; }
        else if (c >= 0xEE && c <= 0xEF) expect = 3;
        else if (c == 0xF0)              { expect = 4; min2 = 0x90; }
        else if (c >= 0xF1 && c <= 0xF3) expect = 4;
        else if (c == 0xF4)              { expect = 4; max2 = 0x8F; }

        bool valid = expect != 0 && i + expect <= len;
        if (valid) {
            unsigned char b2 = (unsigned char)text[i + 1];
            if (b2 < min2 || b2 > max2) valid = false;
            for (size_t k = 2; valid && k < expect; k++) {
                unsigned char cc = (unsigned char)text[i + k];
                if (cc < 0x80 || cc > 0xBF) { valid = false; break; }
            }
        }

        if (valid) {
            for (size_t k = 0; k < expect; k++) {
                out[o++] = text[i + k];
            }
            prev_dash = false;
            i += expect;
        } else {
            out[o++] = '%';
            out[o++] = hex[(c >> 4) & 0xF];
            out[o++] = hex[c & 0xF];
            prev_dash = false;
            i++;
        }
    }

    while (o > 0 && out[o - 1] == '-') o--;
    out[o] = '\0';
    return out;
}

/* If `slug` already appears in `seen->slug_index`, append "-1", "-2", ...
 * until a fresh value is found. Empty slugs are not deduped: they
 * suppress the id attribute entirely at apply time, so deduping them
 * would emit invalid id="-1". After this function returns the new slug
 * is NOT yet recorded in the index; the caller must call
 * heading_list_index_slug once it knows the slug will actually be kept
 * (so a push that fails later doesn't leave a phantom entry).
 *
 * Returns NULL on the (practically unreachable) case of exhausting
 * MDPARSER_DEDUPE_MAX_RETRIES; caller should treat as "drop the id"
 * and continue. The original `slug` is freed in that path too. */
static char *mdparser_dedupe_slug(mdparser_heading_list *seen, char *slug)
{
    if (slug[0] == '\0') return slug;

    if (!heading_list_slug_taken(seen, slug, strlen(slug))) {
        return slug;
    }

    size_t base_len = strlen(slug);
    char *candidate = emalloc(base_len + 24);
    for (unsigned long n = 1; n <= MDPARSER_DEDUPE_MAX_RETRIES; n++) {
        int written = snprintf(candidate, base_len + 24, "%s-%lu", slug, n);
        if (written < 0 || (size_t)written >= base_len + 24) continue;
        if (!heading_list_slug_taken(seen, candidate, (size_t)written)) {
            efree(slug);
            return candidate;
        }
    }
    /* Pathological case (>100k collisions on the same base): drop the
     * slug entirely. apply_transforms emits <hN> without id=. */
    efree(candidate);
    efree(slug);
    char *empty = emalloc(1);
    empty[0] = '\0';
    return empty;
}

/* Forward declaration: resolve_heading_offsets uses scan_skip_region
 * (defined later) to step over comments / CDATA / raw-text element
 * bodies during the heading-fingerprint search. */
static size_t scan_skip_region(const char *html, size_t i, size_t html_len);

/* Sorted, non-overlapping byte ranges covering scan_skip_region
 * territory in the rendered document. Built once per postprocess
 * call so resolve_heading_offsets does not re-walk the document
 * for every heading (was O(N*M); now O(M) precompute + O(N) seek). */
typedef struct {
    size_t start;
    size_t end;
} mdparser_skip_range;

typedef struct {
    mdparser_skip_range *items;
    size_t count;
    size_t cap;
} mdparser_skip_list;

static bool skip_list_push(mdparser_skip_list *l, size_t start, size_t end)
{
    if (l->count == l->cap) {
        size_t new_cap = l->cap ? l->cap * 2 : 16;
        mdparser_skip_range *next = safe_erealloc(l->items, sizeof(*next), new_cap, 0);
        if (!next) return false;
        l->items = next;
        l->cap = new_cap;
    }
    l->items[l->count].start = start;
    l->items[l->count].end = end;
    l->count++;
    return true;
}

static void skip_list_free(mdparser_skip_list *l)
{
    if (l->items) efree(l->items);
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

/* Walk the rendered HTML once, recording every scan_skip_region range
 * in source order. Returns false on allocation failure. */
static bool compute_skip_list(const char *html, size_t html_len,
    mdparser_skip_list *out)
{
    size_t i = 0;
    while (i < html_len) {
        const char *next = memchr(html + i, '<', html_len - i);
        if (!next) break;
        i = (size_t)(next - html);
        size_t end = scan_skip_region(html, i, html_len);
        if (end != SIZE_MAX) {
            if (!skip_list_push(out, i, end)) return false;
            i = (end > i) ? end : i + 1;
        } else {
            i++;
        }
    }
    return true;
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

/* Failure reasons for collect_headings / mdparser_html_postprocess.
 * Distinguishing the AST-depth-cap path from cmark allocation failure
 * lets the caller emit a precise exception message; the wrapper-side
 * "depth cap" failure is a documented limit, not an OOM, so callers
 * trying to recover have different remediations. */
typedef enum {
    MDPP_OK = 0,
    MDPP_DEPTH_CAP,
    MDPP_RENDER_NULL,
    MDPP_ITER_NULL,
    MDPP_ALLOC_FAIL,
} mdparser_pp_status;

/* Walk the document; for each heading node, capture its slug and a
 * standalone HTML rendering. The standalone rendering is later used
 * as a position fingerprint inside the full document HTML. Sets
 * *status_out to the specific failure reason on a non-OK return.
 * `out` is left in a state the caller can pass to heading_list_free
 * regardless. */
static bool collect_headings(cmark_node *document, int cmark_options,
    cmark_llist *extensions, cmark_mem *mem, mdparser_heading_list *out,
    mdparser_pp_status *status_out)
{
    *status_out = MDPP_OK;

    cmark_iter *iter = cmark_iter_new(document);
    if (!iter) {
        *status_out = MDPP_ITER_NULL;
        return false;
    }

    cmark_event_type ev;
    while ((ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        if (ev != CMARK_EVENT_ENTER) continue;
        cmark_node *cur = cmark_iter_get_node(iter);
        if (cmark_node_get_type(cur) != CMARK_NODE_HEADING) continue;

        smart_str tb = {0};
        if (!collect_heading_text(cur, &tb, 0)) {
            smart_str_free(&tb);
            cmark_iter_free(iter);
            *status_out = MDPP_DEPTH_CAP;
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
            *status_out = MDPP_RENDER_NULL;
            return false;
        }

        mdparser_heading_entry e = {
            .slug = slug,
            .rendered = rendered,
            .rendered_len = strlen(rendered),
            .doc_offset = SIZE_MAX,
        };
        if (!heading_list_push(out, e)) {
            mem->free(rendered);
            efree(slug);
            cmark_iter_free(iter);
            *status_out = MDPP_ALLOC_FAIL;
            return false;
        }
        /* Record only after the push succeeds so a failed push doesn't
         * leave a phantom slug in the index. */
        heading_list_index_slug(out, slug, strlen(slug));
    }

    cmark_iter_free(iter);
    return true;
}

/* Resolve each heading entry's doc_offset by sequential memmem in the
 * full document HTML, skipping over scan_skip_region territory so a
 * fingerprint match inside an HTML comment, CDATA, script/style/title/
 * textarea/iframe/etc. body cannot hijack the slug intended for a real
 * Markdown heading. Each search starts past the prior match to keep
 * source order. Entries whose rendered bytes can't be located outside
 * any skip region keep doc_offset=SIZE_MAX and are silently skipped at
 * apply time; that happens for headings whose body contains state-
 * dependent renderer output (e.g. footnote references inside a
 * heading: the standalone footnote_ix is 0 but the in-document index
 * is whatever counter cmark had reached at that point).
 *
 * Skip ranges are pre-computed once into `skips` (sorted by start),
 * so resolution is O(N + M) total instead of O(N*M) in the previous
 * walk-from-cursor-for-each-heading version. */
static void resolve_heading_offsets(const char *html, size_t html_len,
    const mdparser_skip_list *skips, mdparser_heading_list *headings)
{
    size_t cursor = 0;
    size_t region_ix = 0;

    for (size_t hi = 0; hi < headings->count; hi++) {
        mdparser_heading_entry *e = &headings->items[hi];

        while (cursor < html_len) {
            /* Drop regions that ended before the cursor (can happen
             * after a successful heading match advanced cursor past
             * them). */
            while (region_ix < skips->count &&
                   skips->items[region_ix].end <= cursor) {
                region_ix++;
            }

            /* If cursor is inside the current region, jump past it. */
            if (region_ix < skips->count &&
                skips->items[region_ix].start <= cursor &&
                cursor < skips->items[region_ix].end)
            {
                cursor = skips->items[region_ix].end;
                region_ix++;
                continue;
            }

            /* Segment runs from cursor to the next region's start
             * (or html_len if no region remains). */
            size_t seg_end = (region_ix < skips->count)
                ? skips->items[region_ix].start
                : html_len;

            if (e->rendered_len <= seg_end - cursor) {
                const char *hit = memmem(html + cursor, seg_end - cursor,
                    e->rendered, e->rendered_len);
                if (hit) {
                    e->doc_offset = (size_t)(hit - html);
                    cursor = e->doc_offset + e->rendered_len;
                    goto next_heading;
                }
            }
            cursor = seg_end;
        }
next_heading:
        ;
    }
}

/* ---------- HTML transformation -------------------------------------- */

/* If `i` starts a region whose body must be emitted verbatim (raw-text
 * or escapable-raw-text element body, HTML comment, or CDATA section),
 * return the byte offset just past the region's terminator. Otherwise
 * return SIZE_MAX.
 *
 * The set of skipped regions matches HTML5 § 13.2.5.4 raw-text and
 * escapable-raw-text content models (script, style, title, textarea,
 * iframe, noscript, xmp, noembed, noframes — plaintext has no end tag
 * and is treated as extending to EOF) plus comments (`<!-- … -->`)
 * and CDATA (`<![CDATA[ … ]]>`). Without these, attacker-controlled
 * bytes inside any of these regions (reachable under `unsafe:true,
 * tagfilter:false`) match the postprocessor's `<a href="` and
 * `<hN>...</hN>` patterns and either splice attribute injections or
 * hijack heading-id placement.
 *
 * Tag-name match is case-insensitive (raw HTML may be written in any
 * case). For elements with end tags, the region runs from `<tag…>` to
 * the first `</tag>`; an unmatched open extends to end-of-input. */
static size_t scan_skip_region(const char *html, size_t i, size_t html_len)
{
    if (i >= html_len || html[i] != '<') return SIZE_MAX;

    /* HTML comment: <!-- ... -->. Per HTML5 the comment body cannot
     * contain `--` itself, but lenient/legacy parsers accept it; we
     * just skip to the first `-->`. An unterminated comment extends
     * to EOF. */
    if (i + 4 <= html_len && memcmp(html + i, "<!--", 4) == 0) {
        for (size_t j = i + 4; j + 3 <= html_len; j++) {
            if (html[j] == '-' && html[j + 1] == '-' && html[j + 2] == '>') {
                return j + 3;
            }
        }
        return html_len;
    }

    /* CDATA section: <![CDATA[ ... ]]>. Reachable under unsafe:true
     * + tagfilter:false from authored XHTML/SVG content. */
    if (i + 9 <= html_len && memcmp(html + i, "<![CDATA[", 9) == 0) {
        for (size_t j = i + 9; j + 3 <= html_len; j++) {
            if (html[j] == ']' && html[j + 1] == ']' && html[j + 2] == '>') {
                return j + 3;
            }
        }
        return html_len;
    }

    /* Raw-text / escapable-raw-text elements. Each entry is just the
     * tag name; we build the close-tag match in-flight so we can
     * tolerate whitespace before the `>` (HTML5 § 13.2.5.10 allows
     * `</title >`, `</script\n>`, etc.). plaintext has no end tag and
     * is handled as a special case below. */
    static const struct {
        const char *name;
        size_t name_len;
    } tags[] = {
        { "script",   6 },
        { "style",    5 },
        { "title",    5 },
        { "textarea", 8 },
        { "iframe",   6 },
        { "noscript", 8 },
        { "xmp",      3 },
        { "noembed",  7 },
        { "noframes", 8 },
    };

    for (size_t t = 0; t < sizeof(tags) / sizeof(tags[0]); t++) {
        size_t end = i + 1 + tags[t].name_len;
        if (end >= html_len) continue;
        if (mdparser_ascii_strncasecmp(html + i + 1, tags[t].name, tags[t].name_len) != 0) continue;

        char delim = html[end];
        if (delim != '>' && delim != '/' && !mdparser_is_html_space(delim)) continue;

        /* Walk past the opening tag's `>` with quote awareness, so a
         * `</title>`-shaped substring inside an attribute value of
         * the OPENING tag does not prematurely terminate the skip
         * region. e.g. `<title alt="</title>">real</title>` should
         * skip up to the second `</title>`. */
        size_t body_start = end;
        {
            char q = 0;
            while (body_start < html_len) {
                char c = html[body_start];
                if (q) {
                    if (c == q) q = 0;
                    body_start++;
                    continue;
                }
                if (c == '"' || c == '\'') { q = c; body_start++; continue; }
                if (c == '>') { body_start++; break; }
                body_start++;
            }
            if (body_start >= html_len) return html_len;
        }

        /* Scan for `</tagname` followed by zero or more whitespace
         * (space/tab/CR/LF) then `>`. Tag-name match is ASCII case-
         * insensitive. Anything else is just text inside the body. */
        for (size_t j = body_start; j + tags[t].name_len + 3 <= html_len; j++) {
            if (html[j] != '<' || html[j + 1] != '/') continue;
            if (mdparser_ascii_strncasecmp(html + j + 2, tags[t].name, tags[t].name_len) != 0) continue;
            size_t k = j + 2 + tags[t].name_len;
            while (k < html_len) {
                char c = html[k];
                if (c == '>') return k + 1;
                if (mdparser_is_html_space(c)) { k++; continue; }
                break;
            }
            /* Saw `</tagname` but no terminating `>` (or stray
             * non-whitespace). Keep scanning for another `</tagname`
             * candidate. */
        }
        return html_len;
    }

    /* <plaintext>: opens an everything-is-text region that has no
     * close tag and runs to EOF. Match permissively (any delimiter
     * after the tag name). */
    {
        static const char name[] = "plaintext";
        static const size_t name_len = sizeof(name) - 1;
        size_t end = i + 1 + name_len;
        if (end < html_len &&
            mdparser_ascii_strncasecmp(html + i + 1, name, name_len) == 0)
        {
            char delim = html[end];
            if (delim == '>' || delim == '/' || mdparser_is_html_space(delim)) {
                return html_len;
            }
        }
    }

    return SIZE_MAX;
}

/* Given `i` at a `<`, advance to the byte just past the matching `>`
 * of the open tag, treating bytes inside single- or double-quoted
 * attribute values as literal (so `>` inside an attribute does not
 * terminate the tag).
 *
 * Used by apply_transforms to skip past any tag whose interior is
 * not a postprocess injection target. Without this, raw HTML like
 * `<div title='<a href="x">y</a>'>` (reachable under `unsafe:true`)
 * would have the inner `<a href="` matched and rewritten, splicing
 * attributes out of the surrounding `<div>`.
 *
 * Returns SIZE_MAX if `i` does not start a tag (i.e. the next byte
 * after `<` is not in the tag-name / `!` / `?` / `/` start set), so
 * the caller can advance by one. Returns html_len on an unterminated
 * tag (handed to the caller to stop the walk). */
static size_t scan_tag_close(const char *html, size_t i, size_t html_len)
{
    if (i >= html_len || html[i] != '<') return SIZE_MAX;
    if (i + 1 >= html_len) return SIZE_MAX;
    char c2 = html[i + 1];
    bool tag_start = (c2 >= 'a' && c2 <= 'z') ||
                     (c2 >= 'A' && c2 <= 'Z') ||
                     c2 == '/' || c2 == '!' || c2 == '?';
    if (!tag_start) return SIZE_MAX;

    char quote = 0;
    for (size_t j = i + 1; j < html_len; j++) {
        char c = html[j];
        if (quote) {
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c == '>') {
            return j + 1;
        }
    }
    return html_len;
}

/* Apply both postprocess transforms in one linear pass.
 *
 * The hot loop tracks a run of bytes (`run_start..i`) that are emitted
 * verbatim. memchr jumps from `<` to `<` so non-tag bytes never enter
 * the per-byte path. At each `<` we (in order):
 *   1. Skip past raw-text / escapable-raw-text element bodies, HTML
 *      comments, and CDATA via scan_skip_region.
 *   2. If the position matches a heading's doc_offset, inject the id.
 *   3. If the bytes are `<a href="` and not a fragment, inject rel.
 *   4. Otherwise advance past the entire tag (handling quoted
 *      attribute values) so subsequent `<` searches do not land
 *      inside an attribute value -- protects against attacker-
 *      controlled raw HTML like `<div title='<a href="x">'>` where
 *      the inner `<a href="` would otherwise be rewritten and splice
 *      attributes out of the outer tag.
 *
 * Heading anchor positions are pre-computed in
 * `headings->items[k].doc_offset` (resolve_heading_offsets, which
 * also skips regions). */
static zend_string *apply_transforms(const char *html, size_t html_len,
    mdparser_heading_list *headings, int pp_mask)
{
    static const char rel_inject[] = "rel=\"nofollow noopener noreferrer\" ";
    static const size_t rel_inject_len = sizeof(rel_inject) - 1;
    static const char anchor_open[] = "<a href=\"";
    static const size_t anchor_open_len = sizeof(anchor_open) - 1;

    smart_str out = {0};
    smart_str_alloc(&out, html_len + html_len / 4 + 64, 0);

    size_t i = 0;
    size_t run_start = 0;
    size_t heading_ix = 0;
    bool want_anchors = (pp_mask & MDPARSER_PP_HEADING_ANCHORS) != 0;
    bool want_nofollow = (pp_mask & MDPARSER_PP_NOFOLLOW_LINKS) != 0;

    if (want_anchors) {
        while (heading_ix < headings->count &&
               headings->items[heading_ix].doc_offset == SIZE_MAX) {
            heading_ix++;
        }
    }

    while (i < html_len) {
        const char *next_lt = memchr(html + i, '<', html_len - i);
        if (!next_lt) break;
        i = (size_t)(next_lt - html);

        /* Skip-region: script/style/title/textarea/iframe/noscript/
         * xmp/noembed/noframes/plaintext bodies, HTML comments, and
         * CDATA. Their interior is emitted verbatim and is not a
         * valid injection site under any pp_mask. */
        size_t raw_skip = scan_skip_region(html, i, html_len);
        if (raw_skip != SIZE_MAX) {
            while (want_anchors && heading_ix < headings->count &&
                   headings->items[heading_ix].doc_offset != SIZE_MAX &&
                   headings->items[heading_ix].doc_offset < raw_skip)
            {
                heading_ix++;
            }
            i = raw_skip;
            continue;
        }

        if (want_anchors && heading_ix < headings->count &&
            i == headings->items[heading_ix].doc_offset)
        {
            mdparser_heading_entry *e = &headings->items[heading_ix];
            /* Flush run, emit "<hN" and optionally ` id="slug"`. The
             * rest of the open tag (data-sourcepos or just `>`) flows
             * with the next run. Empty slug means the heading
             * slugified to nothing (pure punctuation): emit <hN> with
             * no id rather than id="". */
            if (i > run_start) {
                smart_str_appendl(&out, html + run_start, i - run_start);
            }
            smart_str_appendl(&out, html + i, 3);
            if (e->slug && e->slug[0]) {
                size_t s_len = strlen(e->slug);
                smart_str_appendl(&out, " id=\"", 5);
                smart_str_appendl(&out, e->slug, s_len);
                smart_str_appendc(&out, '"');
            }
            i += 3;
            run_start = i;
            do {
                heading_ix++;
            } while (heading_ix < headings->count &&
                     headings->items[heading_ix].doc_offset == SIZE_MAX);
            continue;
        }

        if (want_nofollow && i + anchor_open_len <= html_len &&
            memcmp(html + i, anchor_open, anchor_open_len) == 0)
        {
            /* Fragment anchors (href="#...") get no rel injection:
             * footnote refs and backrefs jump within the same
             * document, where nofollow is meaningless. */
            bool is_fragment = (i + anchor_open_len < html_len &&
                                html[i + anchor_open_len] == '#');
            if (!is_fragment) {
                if (i > run_start) {
                    smart_str_appendl(&out, html + run_start, i - run_start);
                }
                smart_str_appendl(&out, "<a ", 3);
                smart_str_appendl(&out, rel_inject, rel_inject_len);
                i += 3;
                run_start = i;
                continue;
            }
        }

        /* Advance past the rest of this tag, treating quoted attribute
         * values as literal so a `<` (or `>`) inside an attribute does
         * not pull the next iteration into mid-attribute bytes. */
        size_t tag_end = scan_tag_close(html, i, html_len);
        if (tag_end == SIZE_MAX) {
            i++;
        } else if (tag_end >= html_len) {
            break;
        } else {
            i = tag_end;
        }
    }

    if (html_len > run_start) {
        smart_str_appendl(&out, html + run_start, html_len - run_start);
    }

    smart_str_0(&out);
    return out.s ? out.s : ZSTR_EMPTY_ALLOC();
}

/* ---------- public entry point --------------------------------------- */

/* Failure-reason out-parameter variant. status_out is set to MDPP_OK on
 * success, or to one of the discriminated failure reasons. The legacy
 * variant (no status_out) is the public ABI; callers that want a
 * specific exception message use this variant directly. */
zend_string *mdparser_html_postprocess_ex(
    const char *html_in, size_t html_len,
    cmark_node *document, int cmark_options,
    cmark_llist *extensions, int pp_mask,
    int *status_out)
{
    /* All call sites short-circuit on pp_mask==0 before invoking us;
     * leaving the trivial-copy branch in would mask caller bugs. */
    ZEND_ASSERT(pp_mask != 0);

    mdparser_pp_status status = MDPP_OK;
    mdparser_heading_list headings = {0};

    if (pp_mask & MDPARSER_PP_HEADING_ANCHORS) {
        if (!document) {
            /* Caller bug: anchors requested without an AST. */
            *status_out = (int)MDPP_ALLOC_FAIL;
            return NULL;
        }
        if (!collect_headings(document, cmark_options, extensions,
                              &mdparser_zend_mem, &headings, &status)) {
            heading_list_free(&headings, &mdparser_zend_mem);
            *status_out = (int)status;
            return NULL;
        }
        /* Pre-compute skip ranges once so resolve_heading_offsets
         * does not re-scan the document for every heading. */
        mdparser_skip_list skips = {0};
        if (!compute_skip_list(html_in, html_len, &skips)) {
            skip_list_free(&skips);
            heading_list_free(&headings, &mdparser_zend_mem);
            *status_out = (int)MDPP_ALLOC_FAIL;
            return NULL;
        }
        resolve_heading_offsets(html_in, html_len, &skips, &headings);
        skip_list_free(&skips);
    }

    zend_string *out = apply_transforms(html_in, html_len, &headings, pp_mask);
    heading_list_free(&headings, &mdparser_zend_mem);
    *status_out = (int)MDPP_OK;
    return out;
}

zend_string *mdparser_html_postprocess(
    const char *html_in, size_t html_len,
    cmark_node *document, int cmark_options,
    cmark_llist *extensions, int pp_mask)
{
    int status;
    return mdparser_html_postprocess_ex(html_in, html_len, document,
        cmark_options, extensions, pp_mask, &status);
}

const char *mdparser_pp_status_message(int status)
{
    switch ((mdparser_pp_status)status) {
        case MDPP_OK:
            return NULL;
        case MDPP_DEPTH_CAP:
            return "mdparser: heading text exceeds maximum AST nesting depth";
        case MDPP_RENDER_NULL:
            return "mdparser: cmark heading render returned null during postprocess";
        case MDPP_ITER_NULL:
            return "mdparser: cmark_iter_new returned null during postprocess";
        case MDPP_ALLOC_FAIL:
        default:
            return "mdparser: HTML postprocess allocation failure";
    }
}
