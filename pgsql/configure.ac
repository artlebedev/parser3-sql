dnl Autoconf initialisation
AC_PREREQ(2.59)
AC_INIT(parser3pgsql, 10.7)
AC_CONFIG_SRCDIR(parser3pgsql.C)
AM_CONFIG_HEADER(config_auto.h)

dnl Automake Initialisation
AM_INIT_AUTOMAKE

dnl Checks for argumenrs
AC_DEFUN([PGSQL_INC_CHK],[
	if test -r "$1/libpq-fe.h"; then
		PGSQL_INC=$1
	fi
])

dnl Arguments
AC_ARG_WITH(pgsql-inc,[  --with-pgsql-inc=DIR        DIR is the PgSql includes directory],
	PGSQL_INC_CHK($withval)

	if test -z "$PGSQL_INC"; then
		AC_MSG_ERROR($PGSQL_INC does not seem to be valid PgSql includes directory)
	fi
,
	AC_MSG_CHECKING(for PGSQL_INC directory)
	for d in \
		/usr/include \
		/usr/include/postgresql \
		/usr/local/include \
		/usr/local/include/postgresql \
		/usr/local/pgsql/include ; do
		PGSQL_INC_CHK($d)
	done

	if test -z "$PGSQL_INC"; then
		AC_MSG_ERROR(use --with-pgsql-inc=DIR to specify PgSql includes directory)
	fi

	AC_MSG_RESULT($PGSQL_INC)
)

AC_SUBST(PGSQL_INC)

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
