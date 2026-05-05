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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "zend_exceptions.h"
#include "ext/spl/spl_exceptions.h"

#include "php_mdparser.h"
#include "mdparser_arginfo.h"

zend_class_entry *mdparser_exception_ce;

void mdparser_exception_register_class(void)
{
    mdparser_exception_ce = register_class_MdParser_Exception(spl_ce_RuntimeException);
}
