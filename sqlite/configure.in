dnl Autoconf initialisation
AC_PREREQ(2.59)
AC_INIT(parser3sqlite, 10.3)
AC_CONFIG_SRCDIR(parser3sqlite.C)
AM_CONFIG_HEADER(config_auto.h)

dnl Automake Initialisation
AM_INIT_AUTOMAKE

dnl Checks for argumenrs
AC_DEFUN([SQLITE_INC_CHK],[
	if test -r "$1/sqlite3.h"; then
		SQLITE_INC=$1
	fi
])

dnl Arguments
AC_ARG_WITH(sqlite-inc,[  --with-sqlite-inc=DIR        DIR is the SQLite includes directory],
	SQLITE_INC_CHK($withval)

	if test -z "$SQLITE_INC"; then
		AC_MSG_ERROR($SQLITE_INC does not seem to be valid SQLite includes directory)
	fi
,
	AC_MSG_CHECKING(for SQLITE_INC directory)
	for d in \
		/usr/local/include \
		/usr/include \
		/usr/local/include/sqlite \
		/usr/include/sqlite ; do
		SQLITE_INC_CHK($d)
	done

	if test -z "$SQLITE_INC"; then
		AC_MSG_ERROR(use --with-sqlite-inc=DIR to specify SQLite includes directory)
	fi

	AC_MSG_RESULT($SQLITE_INC)
)

AC_SUBST(SQLITE_INC)

dnl Checks for programs
AC_PROG_INSTALL
AC_PROG_CXX
AC_PROG_CC

dnl Enable building of the convenience library
LT_CONFIG_LTDL_DIR(libltdl)
LT_INIT(disable-static dlopen win32-dll)
LTDL_INIT

dnl Checks for libraries

dnl Checks for header files
AC_CHECK_HEADERS(
stdio.h \
stdlib.h \
string.h \
setjmp.h \
ctype.h
)

dnl Checks for typedefs, structures, and compiler characteristics
AC_TYPE_SIZE_T

dnl Checks for library functions

dnl Output makefiles
AC_OUTPUT(Makefile)
