SUBDIRS = libltdl

LIBTOOL_DEPS = @LIBTOOL_DEPS@
libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck



INCLUDES = -I../parser3/src/sql $(INCLTDL) -I$(MYSQL_INC)

lib_LTLIBRARIES = libparser3mysql.la
libparser3mysql_la_SOURCES = parser3mysql.C parser3mysql.h
libparser3mysql_la_LIBADD = $(LIBLTDL)
libparser3mysql_la_LDFLAGS = -module -avoid-version
