## Process this file with automake to produce Makefile.in
include $(top_srcdir)/flymake.am

AUTOMAKE_OPTIONS = 1.6 foreign
EXTRA_DIST = cutest.h driver.c test_list.c

AM_CPPFLAGS = -I$(top_srcdir)

noinst_LIBRARIES = libunittest.a
noinst_HEADERS = cutest.h

libunittest_a_SOURCES = \
	cutest.h \
	driver.c
