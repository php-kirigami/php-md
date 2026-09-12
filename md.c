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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "ext/standard/info.h"
#include "php_md.h"

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_md_render, 0, 1, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, markdown, IS_STRING, 0)
ZEND_END_ARG_INFO()

/*
 * Placeholder only (see CLAUDE.md "Status"): the real implementation
 * parses via cmark-gfm (cmark_parser_new() with the GFM syntax extensions
 * attached) and renders via cmark_render_html(), covering only the
 * CommonMark+GFM structural core — everything else MD::toHtml() does
 * (plugins, emoji, footnotes, alerts, the raw-HTML whitelist) stays in
 * PHP and is unaffected by this extension. For now this just proves the
 * extension loads and the namespaced MD\Render() function resolves
 * correctly, distinct from (and not colliding with) the global \MD class
 * in kirigami/php-prepros.
 */
PHP_FUNCTION(md_render)
{
	zend_string *markdown;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(markdown)
	ZEND_PARSE_PARAMETERS_END();

	RETURN_STR_COPY(markdown);
}

static const zend_function_entry md_functions[] = {
	ZEND_NS_NAMED_FE("MD", Render, PHP_FN(md_render), arginfo_md_render)
	PHP_FE_END
};

PHP_MINFO_FUNCTION(md)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "md support", "enabled");
	php_info_print_table_row(2, "version", PHP_MD_VERSION);
	php_info_print_table_end();
}

zend_module_entry md_module_entry = {
	STANDARD_MODULE_HEADER,
	"md",
	md_functions,
	NULL,
	NULL,
	NULL,
	NULL,
	PHP_MINFO(md),
	PHP_MD_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_MD
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(md)
#endif
