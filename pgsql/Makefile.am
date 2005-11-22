SUBDIRS = libltdl

LIBTOOL_DEPS = @LIBTOOL_DEPS@
libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck


INCLUDES = -I../../parser3/src/sql $(INCLTDL) -I$(PGSQL_INC)

noinst_HEADERS = config_includes.h
lib_LTLIBRARIES = libparser3pgsql.la
libparser3pgsql_la_SOURCES = parser3pgsql.C
libparser3pgsql_la_LIBADD = $(LIBLTDL)
libparser3pgsql_la_LDFLAGS = -module -avoid-version -lstdc++

EXTRA_DIST=config_fixed.h parser3pgsql.sln parser3pgsql.vcproj

update:
	cvs update
	$(MAKE)

commit:
#	trick to make 'make' happy at check out time
#	without this aclocal.m4 never updates
#	and becomes older than it's prerequisities
#	thus causing redundant remaking:
#	aclocal+autoconf+automake

	cvs commit -m "no message" -f acinclude.m4
	cvs commit -m "no message" -f aclocal.m4
	cvs commit -m "no message" -f Makefile.am
	cvs commit -m "no message" -f Makefile.in
	cvs commit -m "no message" -f configure.in
	cvs commit -m "no message" -f configure

	cvs commit

happy: update install-exec
