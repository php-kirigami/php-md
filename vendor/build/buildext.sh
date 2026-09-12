#!/bin/bash
set -e
cd /mnt/c/projects/kirigami/php-mdhtml
rm -rf autom4te.cache build modules .libs Makefile* config.h config.h.in config.log config.nice config.status configure configure.ac libtool run-tests.php *.lo *.la *.dep
phpize
./configure --with-mdhtml
make -j"$(nproc)" 2>&1 | tail -40
