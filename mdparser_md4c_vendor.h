/*
  +----------------------------------------------------------------------+
  | Copyright (c) 2025-2026, Ilia Alshanetsky                            |
  | Copyright (c) 2025-2026, Advanced Internet Designs Inc.              |
  +----------------------------------------------------------------------+
  | This source file is subject to the BSD 3-Clause license that is      |
  | bundled with this package in the file LICENSE.                       |
  +----------------------------------------------------------------------+
  | Author: Ilia Alshanetsky <ilia@ilia.ws>                              |
  +----------------------------------------------------------------------+
*/

#ifndef MDPARSER_MD4C_VENDOR_H
#define MDPARSER_MD4C_VENDOR_H

#include "php.h"
#include "md4c.h"

/* Runs md_parse() with a per-parse libc allocation registry. Sets *bailed_out
 * when a Zend bailout was caught, and *limit_exceeded when an allocation was
 * refused because the parse crossed mdparser.parse_memory_limit. */
int mdparser_md4c_parse(const MD_CHAR *text, MD_SIZE size,
    const MD_PARSER *parser, void *userdata, bool *bailed_out,
    bool *limit_exceeded);

#endif
