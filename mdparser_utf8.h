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

#ifndef MDPARSER_UTF8_H
#define MDPARSER_UTF8_H

#include <stddef.h>

/* Returns an offset b such that src[0..b) is well-formed UTF-8 made of
 * complete sequences. b may stop short of the first error (or of len on
 * valid input); the caller validates src[b..len) with a scalar decoder.
 * Uses SSSE3 (runtime-detected) on x86-64 and NEON on AArch64, returns 0
 * elsewhere or for inputs too short to vectorize. */
size_t mdparser_utf8_valid_prefix(const unsigned char *src, size_t len);

#endif
