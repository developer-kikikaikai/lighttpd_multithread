SUBDIRS=src doc tests tools

EXTRA_DIST=autogen.sh SConstruct CMakeLists.txt meson.build meson_options.txt README.FreeBSD scripts

ACLOCAL_AMFLAGS = -I m4 -I scripts/m4

distcleancheck_listfiles = find -type f -exec sh -c 'test -f $(srcdir)/{} || echo {}' ';'

AM_DISTCHECK_CONFIGURE_FLAGS=--with-openssl
