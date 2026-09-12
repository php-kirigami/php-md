dnl config.m4 for extension md

PHP_ARG_ENABLE([md],
  [whether to enable md support],
  [AS_HELP_STRING([--enable-md],
    [Enable md support (fast CommonMark + GFM rendering, built on cmark-gfm)])],
  [no])

if test "$PHP_MD" != "no"; then
  dnl vendor/libcmark-gfm is staged by vendor/build/stage.sh (see CLAUDE.md
  dnl "Status", decision 5 option A) — not committed, rebuilt from source
  dnl before every phpize build, same convention as php-wasm-compiler's own
  dnl vendored libs.
  if test ! -f "vendor/libcmark-gfm/lib/libcmark-gfm.a"; then
    AC_MSG_ERROR([vendor/libcmark-gfm not found — run vendor/build/stage.sh first])
  fi

  AC_DEFINE(HAVE_MD, 1, [Whether you have md])

  PHP_ADD_INCLUDE([vendor/libcmark-gfm/include])
  PHP_ADD_LIBRARY_WITH_PATH([cmark-gfm], [vendor/libcmark-gfm/lib], [MD_SHARED_LIBADD])
  PHP_ADD_LIBRARY_WITH_PATH([cmark-gfm-extensions], [vendor/libcmark-gfm/lib], [MD_SHARED_LIBADD])
  PHP_SUBST(MD_SHARED_LIBADD)

  PHP_NEW_EXTENSION(md, md.c, $ext_shared)
fi
