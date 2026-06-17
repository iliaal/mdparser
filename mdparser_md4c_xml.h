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

#ifndef MDPARSER_MD4C_XML_H
#define MDPARSER_MD4C_XML_H

#include "php.h"

/* Render `src` to CommonMark XML via md4c. Returns a caller-owned
 * zend_string on success (*status = 0), NULL on failure (*status set). */
zend_string *mdparser_md4c_render_xml(const char *src, size_t len,
    unsigned parser_flags, bool validate_utf8, int *status);

const char *mdparser_md4c_xml_status_message(int status);

#endif
