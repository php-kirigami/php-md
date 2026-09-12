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

#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_md_render, 0, 1, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, markdown, IS_STRING, 0)
ZEND_END_ARG_INFO()

/* GFM extensions attached to every parse. No footnotes extension here —
 * cmark-gfm supports footnotes as a core parser option (CMARK_OPT_FOOTNOTES),
 * not a syntax_extension (verified against its own src/footnotes.c, which
 * lives in src/ rather than extensions/). See CLAUDE.md decision 2. */
static const char *php_md_gfm_extensions[] = {
	"table", "strikethrough", "autolink", "tagfilter", "tasklist", NULL
};

/*
 * Covers only the CommonMark+GFM structural core (see CLAUDE.md decision
 * 3) — everything else MD::toHtml() does (plugins, emoji, definition
 * lists, GFM-style alerts, the raw-HTML whitelist, heading slugs) stays
 * in PHP and is unaffected by this extension. Deliberately returns a
 * plain HTML string rather than exposing any node tree to PHP (decision
 * 4) — no zend_object involved on the cmark side at all.
 *
 * CMARK_OPT_UNSAFE lets raw HTML through unescaped: MD::toHtml() applies
 * its own, stricter whitelist-based sanitizer afterward (it already does
 * this today for its hand-rolled parser), so this extension must not
 * pre-filter or escape raw HTML itself.
 */
PHP_FUNCTION(md_render)
{
	zend_string *markdown;
	cmark_parser *parser;
	cmark_node *document;
	char *html;
	int options = CMARK_OPT_UNSAFE | CMARK_OPT_FOOTNOTES;
	int i;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(markdown)
	ZEND_PARSE_PARAMETERS_END();

	cmark_gfm_core_extensions_ensure_registered();

	parser = cmark_parser_new(options);

	for (i = 0; php_md_gfm_extensions[i]; i++) {
		cmark_syntax_extension *ext = cmark_find_syntax_extension(php_md_gfm_extensions[i]);
		if (ext) {
			cmark_parser_attach_syntax_extension(parser, ext);
		}
	}

	cmark_parser_feed(parser, ZSTR_VAL(markdown), ZSTR_LEN(markdown));
	document = cmark_parser_finish(parser);

	html = cmark_render_html(document, options, cmark_parser_get_syntax_extensions(parser));

	cmark_node_free(document);
	cmark_parser_free(parser);

	if (!html) {
		RETURN_EMPTY_STRING();
	}

	/* cmark_render_html allocates with the default (libc) allocator since
	 * we used cmark_parser_new() rather than cmark_parser_new_with_mem() —
	 * must free() it, not efree(), after copying into a zend_string. */
	RETVAL_STRING(html);
	free(html);
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
