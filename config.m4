dnl config.m4 for extension md

PHP_ARG_ENABLE([md],
  [whether to enable md support],
  [AS_HELP_STRING([--enable-md],
    [Enable md support (fast CommonMark + GFM rendering, built on cmark-gfm)])],
  [no])

if test "$PHP_MD" != "no"; then
  dnl TODO (see CLAUDE.md "Status"): once libcmark-gfm is vendored, add its
  dnl include/lib paths here (either via a vendored ./vendor/libcmark-gfm
  dnl tree, decision 5 option A, or an externally-provided path, option B)
  dnl and link against it, e.g.:
  dnl   PHP_ADD_INCLUDE(vendor/libcmark-gfm/include)
  dnl   PHP_ADD_LIBRARY_WITH_PATH(cmark-gfm, vendor/libcmark-gfm/lib, MD_SHARED_LIBADD)
  dnl   PHP_ADD_LIBRARY_WITH_PATH(cmark-gfm-extensions, vendor/libcmark-gfm/lib, MD_SHARED_LIBADD)
  dnl   PHP_SUBST(MD_SHARED_LIBADD)
  AC_DEFINE(HAVE_MD, 1, [Whether you have md])

  PHP_NEW_EXTENSION(md, md.c, $ext_shared)
fi
