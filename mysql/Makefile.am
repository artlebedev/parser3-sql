SUBDIRS = libltdl

LIBTOOL_DEPS = @LIBTOOL_DEPS@
libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck



INCLUDES = -I../../parser3/src/sql $(INCLTDL) -I$(MYSQL_INC)

noinst_HEADERS = config_includes.h
lib_LTLIBRARIES = libparser3mysql.la
libparser3mysql_la_SOURCES = parser3mysql.C
libparser3mysql_la_LIBADD = $(LIBLTDL)
libparser3mysql_la_LDFLAGS = -module -avoid-version

EXTRA_DIST=config_fixed.h
