SUBDIRS = libltdl

LIBTOOL_DEPS = @LIBTOOL_DEPS@
libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck



INCLUDES = -I../parser3/src/sql $(INCLTDL) -I$(PGSQL_INC)

lib_LTLIBRARIES = libparser3pgsql.la
libparser3pgsql_la_SOURCES = parser3pgsql.C config_includes.h
libparser3pgsql_la_LIBADD = $(LIBLTDL)
libparser3pgsql_la_LDFLAGS = -module -avoid-version
