/*
  +----------------------------------------------------------------------+
  | php-mdhtml                                                            |
  +----------------------------------------------------------------------+
  | Copyright (c) Maxime Larrivée-Roy                                     |
  +----------------------------------------------------------------------+
  | This source file is subject to version 2 of the GNU General Public   |
  | License, that is bundled with this package in the file LICENSE.      |
  +----------------------------------------------------------------------+
*/

#ifndef PHP_MDHTML_H
#define PHP_MDHTML_H

extern zend_module_entry mdhtml_module_entry;
#define phpext_mdhtml_ptr &mdhtml_module_entry

#define PHP_MDHTML_VERSION "0.1.0-dev"

#ifdef PHP_WIN32
# define PHP_MDHTML_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
# define PHP_MDHTML_API __attribute__ ((visibility("default")))
#else
# define PHP_MDHTML_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

PHP_MINIT_FUNCTION(mdhtml);
PHP_MSHUTDOWN_FUNCTION(mdhtml);
PHP_RINIT_FUNCTION(mdhtml);
PHP_RSHUTDOWN_FUNCTION(mdhtml);
PHP_MINFO_FUNCTION(mdhtml);

ZEND_BEGIN_MODULE_GLOBALS(mdhtml)
	HashTable emoji_builtin;
	HashTable emoji_custom;
	HashTable plugins;
ZEND_END_MODULE_GLOBALS(mdhtml)

ZEND_EXTERN_MODULE_GLOBALS(mdhtml)
#define MDHTML_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(mdhtml, v)

#endif /* PHP_MDHTML_H */
