SUBDIRS = libltdl

LIBTOOL_DEPS = @LIBTOOL_DEPS@
libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck


INCLUDES = -I../parser3/src/sql $(INCLTDL) -I$(ORACLE_OCI_INC) -I$(ORACLE_PUBLIC_INC)

noinst_HEADERS = config_includes.h
lib_LTLIBRARIES = libparser3oracle.la
libparser3oracle_la_SOURCES = parser3oracle.C
libparser3oracle_la_LIBADD = $(LIBLTDL)
libparser3oracle_la_LDFLAGS = -module -avoid-version

EXTRA_DIST=config_fixed.h
