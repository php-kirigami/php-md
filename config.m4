dnl config.m4 for extension mdhtml

PHP_ARG_WITH([mdhtml],
  [for mdhtml support],
  [AS_HELP_STRING([--with-mdhtml[=DIR]],
    [Enable mdhtml support (fast CommonMark + GFM rendering, built on
     cmark-gfm). DIR is a prefix containing include/ and lib/ for
     libcmark-gfm + libcmark-gfm-extensions; defaults to
     ./vendor/libcmark-gfm, staged by vendor/build/stage.sh — pass an
     explicit DIR when those static libraries were built/staged
     elsewhere (e.g. php-wasm-compiler's own /root/lib convention)])],
  [no])

if test "$PHP_MDHTML" != "no"; then
  dnl Native dev convenience (bare --with-mdhtml / --enable-mdhtml-style
  dnl "yes"): use the local vendor/libcmark-gfm tree staged by
  dnl vendor/build/stage.sh (see CLAUDE.md "Status", decision 5 option A)
  dnl — not committed, rebuilt from source before every phpize build.
  dnl An explicit --with-mdhtml=DIR (e.g. php-wasm-compiler's /root/lib)
  dnl points at an externally-staged libcmark-gfm instead.
  if test "$PHP_MDHTML" = "yes"; then
    MDHTML_LIBCMARKGFM_DIR=vendor/libcmark-gfm
  else
    MDHTML_LIBCMARKGFM_DIR=$PHP_MDHTML
  fi

  if test ! -f "$MDHTML_LIBCMARKGFM_DIR/lib/libcmark-gfm.a"; then
    AC_MSG_ERROR([libcmark-gfm not found at $MDHTML_LIBCMARKGFM_DIR/lib/libcmark-gfm.a -- run vendor/build/stage.sh, or pass --with-mdhtml=DIR])
  fi

  AC_DEFINE(HAVE_MDHTML, 1, [Whether you have mdhtml])

  PHP_ADD_INCLUDE([$MDHTML_LIBCMARKGFM_DIR/include])
  PHP_ADD_LIBRARY_WITH_PATH([cmark-gfm], [$MDHTML_LIBCMARKGFM_DIR/lib], [MDHTML_SHARED_LIBADD])
  PHP_ADD_LIBRARY_WITH_PATH([cmark-gfm-extensions], [$MDHTML_LIBCMARKGFM_DIR/lib], [MDHTML_SHARED_LIBADD])
  PHP_SUBST(MDHTML_SHARED_LIBADD)

  PHP_NEW_EXTENSION(mdhtml, mdhtml.c, $ext_shared)
fi
