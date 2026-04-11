dnl config.m4 for extension mdparser

PHP_ARG_ENABLE(mdparser, whether to enable mdparser support,
[  --enable-mdparser       Enable mdparser (CommonMark + GFM) support])

PHP_ARG_ENABLE(mdparser-dev, whether to enable developer build flags,
[  --enable-mdparser-dev   Upgrade wrapper warnings to -Werror plus strict checks], no, no)

if test "$PHP_MDPARSER" != "no"; then

  PHP_VERSION_ID=$($PHP_CONFIG --vernum)
  if test "$PHP_VERSION_ID" -lt "80300"; then
    AC_MSG_ERROR([mdparser requires PHP 8.3.0 or later (found $PHP_VERSION_ID)])
  fi

  CMARK_SRC_DIR=vendor/cmark/src
  CMARK_EXT_DIR=vendor/cmark/extensions

  CMARK_SOURCES="\
    $CMARK_SRC_DIR/cmark.c \
    $CMARK_SRC_DIR/blocks.c \
    $CMARK_SRC_DIR/inlines.c \
    $CMARK_SRC_DIR/scanners.c \
    $CMARK_SRC_DIR/utf8.c \
    $CMARK_SRC_DIR/buffer.c \
    $CMARK_SRC_DIR/references.c \
    $CMARK_SRC_DIR/render.c \
    $CMARK_SRC_DIR/node.c \
    $CMARK_SRC_DIR/iterator.c \
    $CMARK_SRC_DIR/commonmark.c \
    $CMARK_SRC_DIR/plaintext.c \
    $CMARK_SRC_DIR/html.c \
    $CMARK_SRC_DIR/xml.c \
    $CMARK_SRC_DIR/latex.c \
    $CMARK_SRC_DIR/man.c \
    $CMARK_SRC_DIR/houdini_href_e.c \
    $CMARK_SRC_DIR/houdini_html_e.c \
    $CMARK_SRC_DIR/houdini_html_u.c \
    $CMARK_SRC_DIR/cmark_ctype.c \
    $CMARK_SRC_DIR/arena.c \
    $CMARK_SRC_DIR/linked_list.c \
    $CMARK_SRC_DIR/map.c \
    $CMARK_SRC_DIR/plugin.c \
    $CMARK_SRC_DIR/registry.c \
    $CMARK_SRC_DIR/syntax_extension.c \
    $CMARK_SRC_DIR/footnotes.c"

  CMARK_EXT_SOURCES="\
    $CMARK_EXT_DIR/core-extensions.c \
    $CMARK_EXT_DIR/table.c \
    $CMARK_EXT_DIR/strikethrough.c \
    $CMARK_EXT_DIR/tasklist.c \
    $CMARK_EXT_DIR/autolink.c \
    $CMARK_EXT_DIR/tagfilter.c \
    $CMARK_EXT_DIR/ext_scanners.c"

  WRAPPER_SOURCES="mdparser.c mdparser_parser.c mdparser_options.c mdparser_exception.c mdparser_ast.c"

  dnl -Wall -Wextra are on by default so wrapper regressions get caught
  dnl in every local build; --enable-mdparser-dev upgrades warnings to
  dnl -Werror plus extra strictness. -Wno-unused-parameter silences
  dnl noise from cmark's own callback-style APIs that share the same
  dnl translation-unit flags with our wrapper.
  MDPARSER_CFLAGS="-DCMARK_GFM_STATIC_DEFINE -DCMARK_GFM_EXTENSIONS_STATIC_DEFINE \
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-function"

  dnl -Wshadow is intentionally NOT enabled; PHP's own headers
  dnl (php_streams.h on 8.5) declare struct members named `zval`
  dnl which shadow the zval typedef, and we can't fix that upstream.
  if test "$PHP_MDPARSER_DEV" = "yes"; then
    MDPARSER_CFLAGS="$MDPARSER_CFLAGS -Werror -Wstrict-prototypes"
  fi

  PHP_NEW_EXTENSION(mdparser,
    $WRAPPER_SOURCES $CMARK_SOURCES $CMARK_EXT_SOURCES,
    $ext_shared,,
    $MDPARSER_CFLAGS)

  PHP_ADD_INCLUDE([$ext_srcdir/$CMARK_SRC_DIR])
  PHP_ADD_INCLUDE([$ext_srcdir/$CMARK_EXT_DIR])
  PHP_ADD_BUILD_DIR([$ext_builddir/$CMARK_SRC_DIR], 1)
  PHP_ADD_BUILD_DIR([$ext_builddir/$CMARK_EXT_DIR], 1)
fi
