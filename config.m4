dnl config.m4 for extension mdparser

PHP_ARG_ENABLE(mdparser, whether to enable mdparser support,
[  --enable-mdparser       Enable mdparser (CommonMark + GFM) support])

PHP_ARG_ENABLE(mdparser-dev, whether to enable developer build flags,
[  --enable-mdparser-dev   Upgrade wrapper warnings to -Werror plus strict checks], no, no)

if test "$PHP_MDPARSER" != "no"; then

  PHP_VERSION_ID=$($PHP_CONFIG --vernum)
  if test "$PHP_VERSION_ID" -lt "80200"; then
    AC_MSG_ERROR([mdparser requires PHP 8.2.0 or later (found $PHP_VERSION_ID)])
  fi

  dnl md4c (https://github.com/mity/md4c, MIT) is the parsing backend. It is
  dnl a single-file push parser plus its bundled HTML entity table; the HTML
  dnl renderer (md4c-html.c) is vendored but not compiled; we use our own
  dnl callback renderer (safe-mode, heading anchors, nofollow, smart
  dnl punctuation). entity.c is still needed for named-entity decoding.
  dnl See vendor/VENDOR.md.
  MD4C_SRC_DIR=vendor/md4c
  MD4C_SOURCES="\
    $MD4C_SRC_DIR/md4c.c \
    $MD4C_SRC_DIR/entity.c"

  WRAPPER_SOURCES="mdparser.c mdparser_parser.c mdparser_options.c mdparser_exception.c mdparser_md4c_html.c mdparser_md4c_ast.c mdparser_md4c_xml.c mdparser_md4c_util.c"

  dnl -Wall -Wextra are on by default so wrapper regressions get caught in
  dnl every local build; --enable-mdparser-dev upgrades warnings to -Werror.
  dnl -Wno-unused-parameter / -Wno-unused-function silence noise from md4c's
  dnl callback-style APIs and bundled static helpers we don't reach.
  dnl -fvisibility=hidden keeps vendored md4c symbols and our internals out of
  dnl the .so's dynamic symbol table (only get_module needs exporting).
  MDPARSER_CFLAGS="-fvisibility=hidden \
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-function"

  dnl -Wshadow is intentionally NOT enabled; PHP's own headers
  dnl (php_streams.h on 8.5) declare struct members named `zval`
  dnl which shadow the zval typedef, and we can't fix that upstream.
  if test "$PHP_MDPARSER_DEV" = "yes"; then
    MDPARSER_CFLAGS="$MDPARSER_CFLAGS -Werror -Wstrict-prototypes"
  fi

  PHP_NEW_EXTENSION(mdparser,
    $WRAPPER_SOURCES $MD4C_SOURCES,
    $ext_shared,,
    $MDPARSER_CFLAGS)

  PHP_ADD_INCLUDE([$ext_srcdir/$MD4C_SRC_DIR])
  PHP_ADD_BUILD_DIR([$ext_builddir/$MD4C_SRC_DIR], 1)
fi
