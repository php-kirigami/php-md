/*
  +----------------------------------------------------------------------+
  | php-md                                                                |
  +----------------------------------------------------------------------+
  | Copyright (c) Maxime Larrivée-Roy                                     |
  +----------------------------------------------------------------------+
  | This source file is subject to version 2 of the GNU General Public   |
  | License, that is bundled with this package in the file LICENSE.      |
  +----------------------------------------------------------------------+
*/

#ifndef PHP_MD_H
#define PHP_MD_H

extern zend_module_entry md_module_entry;
#define phpext_md_ptr &md_module_entry

#define PHP_MD_VERSION "0.1.0-dev"

#ifdef PHP_WIN32
# define PHP_MD_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
# define PHP_MD_API __attribute__ ((visibility("default")))
#else
# define PHP_MD_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

PHP_MINFO_FUNCTION(md);

#endif /* PHP_MD_H */
