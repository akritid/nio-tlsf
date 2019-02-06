ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = nio-tlsf.pc nio-tlsf.spec

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = nio-tlsf.pc

AM_CFLAGS = -Wall -std=c11

lib_LTLIBRARIES = libnio-tlsf.la

libnio_tlsf_la_LDFLAGS = -version-info $(TLSF_CURRENT):$(TLSF_REVISION):$(TLSF_AGE)
libnio_tlsf_la_SOURCES = tlsf.c asan.h

include_HEADERS = tlsf.h

noinst_PROGRAMS = example

example_SOURCES = example.c $(HEADERS)
example_LDADD = $(top_builddir)/libnio-tlsf.la
