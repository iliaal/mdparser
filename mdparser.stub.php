<?php

/** @generate-class-entries */

namespace MdParser;

final class Exception extends \RuntimeException
{
}

/**
 * IMPORTANT: default values below MUST agree with the `default_value`
 * column in `mdparser_options_fields[]` inside `mdparser_options.c`.
 * ZPP does not auto-apply arginfo defaults to internal methods, so
 * the C constructor seeds values[] from the C table and this stub is
 * only used for reflection / IDE signatures. If you change a default,
 * change it in both places.
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
    ) {}

    /**
     * Maximum-safety preset: the standard defaults plus autolink off,
     * so bare URLs in untrusted input do not get wrapped in live <a>
     * tags. Use for forum comments, email rendering, or any rendering
     * path where the source is untrusted and link creation should be
     * explicit.
     */
    public static function strict(): Options {}

    /**
     * GitHub-flavored preset: standard defaults plus footnotes on, to
     * match the feature set github.com renders for README files and
     * issue comments. Everything else (tables, strikethrough,
     * tasklist, autolink, tagfilter) is already on in the default
     * constructor.
     */
    public static function github(): Options {}

    /**
     * Trusted-input preset: raw HTML passthrough (unsafe: true),
     * tagfilter disabled, and liberal HTML tag parsing. Use only when
     * the markdown source is authored by you or comes from a trusted
     * pipeline; this preset explicitly disables the XSS safety net.
     */
    public static function permissive(): Options {}
}

final class Parser
{
    public readonly Options $options;

    public function __construct(?Options $options = null) {}

    public function toHtml(string $source): string {}

    public function toXml(string $source): string {}

    public function toAst(string $source): array {}
}
