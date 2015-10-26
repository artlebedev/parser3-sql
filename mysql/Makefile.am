SUBDIRS = libltdl
ACLOCAL_AMFLAGS = -I libltdl/m4

AM_CPPFLAGS = -I../../parser3/src/sql $(INCLTDL) -I$(MYSQL_INC)

noinst_HEADERS = config_includes.h
lib_LTLIBRARIES = libparser3mysql.la
libparser3mysql_la_SOURCES = parser3mysql.C
libparser3mysql_la_LDFLAGS = -module -avoid-version
libparser3mysql_la_LIBADD = $(LIBLTDL)

EXTRA_DIST=config_fixed.h parser3mysql.sln parser3mysql.vcproj

commit:
#	trick to make 'make' happy at check out time
#	and avoid redundant remaking: aclocal+autoconf+automake

	cvs commit -m "no message" -f aclocal.m4 Makefile.am Makefile.in configure.am configure config_auto.h.in
