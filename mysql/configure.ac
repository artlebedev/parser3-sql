dnl Autoconf initialisation
AC_PREREQ(2.59)
AC_INIT(parser3mysql, 10.9)
AC_CONFIG_SRCDIR(parser3mysql.C)
AM_CONFIG_HEADER(config_auto.h)

dnl Automake Initialisation
AM_INIT_AUTOMAKE

dnl Checks for argumenrs
AC_DEFUN([MYSQL_INC_CHK],[
	if test -r "$1/mysql.h"; then
		MYSQL_INC=$1
	fi
])

dnl Arguments
AC_ARG_WITH(mysql-inc,[  --with-mysql-inc=DIR        DIR is the MySQL includes directory],
	MYSQL_INC_CHK($withval)

	if test -z "$MYSQL_INC"; then
		AC_MSG_ERROR($MYSQL_INC does not seem to be valid MySQL includes directory)
	fi
,
	AC_MSG_CHECKING(for MYSQL_INC directory)
	for d in \
		/usr/local/include/mysql \
		/usr/include/mariadb \
		/usr/include/mysql ; do
		MYSQL_INC_CHK($d)
	done

	if test -z "$MYSQL_INC"; then
		AC_MSG_ERROR(use --with-mysql-inc=DIR to specify MySQL includes directory)
	fi

	AC_MSG_RESULT($MYSQL_INC)
)

AC_SUBST(MYSQL_INC)

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
