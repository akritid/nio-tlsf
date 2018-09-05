ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = nio-tlsf.pc nio-tlsf.spec

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = nio-tlsf.pc

#AM_CFLAGS = -Wall -std=gnu11 -g -O3 -fsanitize=address
AM_CFLAGS = -Wall -std=gnu11 -g -O3

lib_LTLIBRARIES = libnio-tlsf.la libnio-tlsf-ori.la

libnio_tlsf_la_LDFLAGS = -version-info $(TLSF_CURRENT):$(TLSF_REVISION):$(TLSF_AGE)
libnio_tlsf_la_SOURCES = tlsf.c asan.h

libnio_tlsf_ori_la_LDFLAGS = -version-info $(TLSF_ORI_CURRENT):$(TLSF_ORI_REVISION):$(TLSF_ORI_AGE)
libnio_tlsf_ori_la_SOURCES = tlsf_ori.c asan.h

include_HEADERS = tlsf.h tlsf_ori.h

noinst_PROGRAMS = example tlsf_bench

example_SOURCES = example.c $(HEADERS)
example_LDADD = $(top_builddir)/libnio-tlsf.la

tlsf_bench_SOURCES = tlsf_bench.c $(HEADERS)
tlsf_bench_LDADD = $(top_builddir)/libnio-tlsf.la $(top_builddir)/libnio-tlsf-ori.la -lpthread
