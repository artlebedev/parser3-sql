SUBDIRS = libltdl

INCLUDES = -I../../parser3/src/sql $(INCLTDL) -I$(SQLITE_INC)

noinst_HEADERS = config_includes.h
lib_LTLIBRARIES = libparser3sqlite.la
libparser3sqlite_la_SOURCES = parser3sqlite.C
libparser3sqlite_la_LIBADD = $(LIBLTDL)
libparser3sqlite_la_LDFLAGS = -module -avoid-version
libparser3sqlite_la_LINK = $(LIBTOOL) --tag=CC --mode=link $(CXXLD) $(CXXFLAGS) $(libparser3mysql_la_LDFLAGS) $(LDFLAGS) -o $@

EXTRA_DIST=config_fixed.h parser3sqlite.sln parser3sqlite.vcproj

update:
	cvs update
	$(MAKE)

commit:
#	trick to make 'make' happy at check out time
#	without this aclocal.m4 never updates
#	and becomes older than it's prerequisities
#	thus causing redundant remaking:
#	aclocal+autoconf+automake

	cvs commit -m "no message" -f aclocal.m4
	cvs commit -m "no message" -f Makefile.am
	cvs commit -m "no message" -f Makefile.in
	cvs commit -m "no message" -f configure.in
	cvs commit -m "no message" -f configure

	cvs commit

happy: update install-exec
