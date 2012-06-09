SUBDIRS = libltdl
ACLOCAL_AMFLAGS = -I libltdl/m4

INCLUDES = -I../../parser3/src/sql $(INCLTDL) -I$(ORACLE_OCI_INC) -I$(ORACLE_PUBLIC_INC)

noinst_HEADERS = config_includes.h
lib_LTLIBRARIES = libparser3oracle.la
libparser3oracle_la_SOURCES = parser3oracle.C
libparser3oracle_la_LIBADD = $(LIBLTDL)
libparser3oracle_la_LDFLAGS = -module -avoid-version
libparser3oracle_la_LINK = $(LIBTOOL) --tag=CC --mode=link $(CXXLD) $(CXXFLAGS) $(libparser3mysql_la_LDFLAGS) $(LDFLAGS) -o $@

EXTRA_DIST=config_fixed.h parser3oracle.sln parser3oracle.vcproj

commit:
#	trick to make 'make' happy at check out time
#	and avoid redundant remaking: aclocal+autoconf+automake

	cvs commit -m "no message" -f aclocal.m4 Makefile.am Makefile.in configure.in configure config_auto.h.in
