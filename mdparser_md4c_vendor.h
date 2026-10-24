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
 * when a Zend bailout was caught, *limit_exceeded when an allocation was
 * refused because the parse crossed mdparser.parse_memory_limit, and
 * *alloc_failed when libc malloc/realloc returned NULL without a budget
 * refusal (genuine out-of-memory, including the SIZE_MAX overflow guard).
 * Either memory signal maps to ERR_MEMORY at the render entries; only the
 * limit path claims the limit in the message (see the shared OOM wording). */
int mdparser_md4c_parse(const MD_CHAR *text, MD_SIZE size,
    const MD_PARSER *parser, void *userdata, bool *bailed_out,
    bool *limit_exceeded, bool *alloc_failed);

/* Engines that define ZEND_HAS_OOM() no longer bail out of an allocation that
 * crosses memory_limit: the allocation succeeds, the error is queued, and the
 * engine raises it once control returns to the VM. mdparser_md4c_parse()
 * aborts md4c at the next callback when one is queued; render entries must
 * then return without throwing so the engine reports the memory-limit error.
 * Older engines bail out of the allocation instead, so nothing is ever
 * queued there. */
static zend_always_inline bool mdparser_md4c_oom_pending(void)
{
#ifdef ZEND_HAS_OOM
    return ZEND_HAS_OOM();
#else
    return false;
#endif
}

#endif
