SUBDIRS = libltdl
ACLOCAL_AMFLAGS = -I libltdl/m4

INCLUDES = -I../../parser3/src/sql $(INCLTDL) -I$(SQLITE_INC)

noinst_HEADERS = config_includes.h
lib_LTLIBRARIES = libparser3sqlite.la
libparser3sqlite_la_SOURCES = parser3sqlite.C
libparser3sqlite_la_LDFLAGS = -module -avoid-version
libparser3sqlite_la_LIBADD = $(LIBLTDL)

EXTRA_DIST=config_fixed.h parser3sqlite.sln parser3sqlite.vcproj

commit:
#	trick to make 'make' happy at check out time
#	and avoid redundant remaking: aclocal+autoconf+automake

	cvs commit -m "no message" -f aclocal.m4 Makefile.am Makefile.in configure.in configure config_auto.h.in
