<?php

/** @generate-class-entries */

namespace MdParser;

final class Exception extends \RuntimeException
{
}

/**
 * Default values below MUST agree with the `default_value` column in
 * `mdparser_options_fields[]` in `mdparser_options.c`. The C constructor
 * seeds values from that table; this stub only feeds reflection and IDE
 * signatures. Change a default in both places.
 */
final readonly class Options
{
    public bool $sourcepos;
    public bool $hardbreaks;
    public bool $nobreaks;
    public bool $smart;
    public bool $unsafe;
    public bool $validateUtf8;
    public bool $githubPreLang;
    public bool $liberalHtmlTag;
    public bool $footnotes;
    public bool $strikethroughDoubleTilde;
    public bool $tablePreferStyleAttributes;
    public bool $fullInfoString;
    public bool $tables;
    public bool $strikethrough;
    public bool $tasklist;
    public bool $autolink;
    public bool $tagfilter;
    public bool $headingAnchors;
    public bool $nofollowLinks;
    public bool $noIndentedCodeBlocks;
    public bool $permissiveAtxHeadings;
    public bool $collapseWhitespace;
    public bool $underline;
    public bool $highlight;
    public bool $superscript;
    public bool $subscript;
    public bool $spoilers;
    public bool $latexMath;
    public bool $wikiLinks;
    public bool $admonitions;
    public bool $insert;
    public bool $preserveBlankLines;

    /**
     * Inert but accepted for compatibility (no effect): $sourcepos,
     * $githubPreLang, $liberalHtmlTag, $strikethroughDoubleTilde,
     * $tablePreferStyleAttributes, $fullInfoString.
     */
    public function __construct(
        bool $sourcepos = false,
        bool $hardbreaks = false,
        bool $nobreaks = false,
        bool $smart = false,
        bool $unsafe = false,
        bool $validateUtf8 = true,
        bool $githubPreLang = true,
        bool $liberalHtmlTag = false,
        bool $footnotes = false,
        bool $strikethroughDoubleTilde = false,
        bool $tablePreferStyleAttributes = false,
        bool $fullInfoString = false,
        bool $tables = true,
        bool $strikethrough = true,
        bool $tasklist = true,
        bool $autolink = true,
        bool $tagfilter = true,
        bool $headingAnchors = false,
        bool $nofollowLinks = false,
        bool $noIndentedCodeBlocks = false,
        bool $permissiveAtxHeadings = false,
        bool $collapseWhitespace = false,
        bool $underline = false,
        bool $highlight = false,
        bool $superscript = false,
        bool $subscript = false,
        bool $spoilers = false,
        bool $latexMath = false,
        bool $wikiLinks = false,
        bool $admonitions = false,
        bool $insert = false,
        bool $preserveBlankLines = false,
    ) {}

    /**
     * Maximum-safety preset: the standard defaults plus autolink off, so
     * bare URLs in untrusted input stay plain text. Use it for forum
     * comments, email rendering, or other untrusted sources where links
     * should be explicit.
     */
    public static function strict(): Options {}

    /**
     * GitHub-flavored preset: the standard defaults plus footnotes and
     * alerts, matching what github.com renders for READMEs and issue
     * comments.
     */
    public static function github(): Options {}

    /**
     * Trusted-input preset: raw HTML passthrough (unsafe: true) with
     * tagfilter disabled. Disables XSS protection; use it only for
     * markdown you author or that comes from a trusted pipeline.
     */
    public static function permissive(): Options {}
}

final class Parser
{
    public readonly Options $options;

    public function __construct(?Options $options = null) {}

    public function toHtml(string $source): string {}

    /**
     * Returns CommonMark XML for the parsed document. This is a
     * structural representation, not sanitized HTML: raw HTML nodes are
     * XML-escaped but their source literals are preserved; link/image
     * destinations are entity-decoded then XML-escaped. The `unsafe`,
     * `tagfilter`, and URL-scheme defenses don't apply, same as for
     * `toAst()`.
     */
    public function toXml(string $source): string {}

    /**
     * Returns a structural representation of the markdown source as
     * a nested array. Raw HTML literals (`html_block` / `html_inline`)
     * are preserved byte-for-byte. Link and image `url` / `title` fields
     * are entity-decoded but not scheme-filtered. The `unsafe`,
     * `tagfilter`, and URL-scheme defenses apply only to `toHtml` /
     * `toInlineHtml`, not to `toXml` or `toAst`. Consumers that emit
     * HTML from XML or the AST must apply
     * their own URL scheme allowlist and HTML sanitization.
     */
    public function toAst(string $source): array {}

    /**
     * Render `$source` as inline-only HTML: no `<p>` wrapper and no
     * block-level constructs, for short strings such as chat messages,
     * table cells, and display names. Block markers like `#`, `-`, `>`,
     * `1.` are emitted as literal text. Matches Parsedown::line() and
     * cebe/markdown::parseParagraph().
     *
     * `headingAnchors` has no effect here; `nofollowLinks` still applies.
     * Empty or whitespace-only input returns the empty string. Literal
     * U+200B (zero-width space) bytes in the source are preserved.
     */
    public function toInlineHtml(string $source): string {}

    /**
     * Static shortcut: parse `$source` with the default Options and
     * return HTML. Equivalent to `(new Parser)->toHtml($source)`, like
     * michelf/php-markdown's `Markdown::defaultTransform()`.
     */
    public static function html(string $source): string {}

    /**
     * Static shortcut: parse `$source` with the default Options and
     * return CommonMark XML. Like `toXml()`, this preserves raw HTML
     * node literals as escaped XML text.
     */
    public static function xml(string $source): string {}

    /**
     * Static shortcut: parse `$source` with the default Options and
     * return the nested-array AST.
     */
    public static function ast(string $source): array {}
}
