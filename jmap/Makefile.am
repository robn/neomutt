## Process this file with automake to produce Makefile.in
include $(top_srcdir)/flymake.am

AUTOMAKE_OPTIONS = 1.6 foreign

AM_CPPFLAGS = -I$(top_srcdir)

noinst_LIBRARIES = libjmap.a
noinst_HEADERS = jmap_internal.h

libjmap_a_SOURCES = mailbox.c message.c jmap.c jmap.h
