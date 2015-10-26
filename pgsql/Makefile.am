SUBDIRS = libltdl
ACLOCAL_AMFLAGS = -I libltdl/m4

AM_CPPFLAGS = -I../../parser3/src/sql $(INCLTDL) -I$(PGSQL_INC)

noinst_HEADERS = config_includes.h
lib_LTLIBRARIES = libparser3pgsql.la
libparser3pgsql_la_SOURCES = parser3pgsql.C
libparser3pgsql_la_LDFLAGS = -module -avoid-version
libparser3pgsql_la_LIBADD = $(LIBLTDL)

EXTRA_DIST=config_fixed.h parser3pgsql.sln parser3pgsql.vcproj

commit:
#	trick to make 'make' happy at check out time
#	and avoid redundant remaking: aclocal+autoconf+automake

	cvs commit -m "no message" -f aclocal.m4 Makefile.am Makefile.in configure.ac configure config_auto.h.in
