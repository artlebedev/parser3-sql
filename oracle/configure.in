dnl Autoconf initialisation
AC_PREREQ(2.59)
AC_INIT(parser3oracle, 10.4)
AC_CONFIG_SRCDIR(parser3oracle.C)
AM_CONFIG_HEADER(config_auto.h)

dnl Automake Initialisation
AM_INIT_AUTOMAKE

dnl Arguments
AC_ARG_WITH(oracleoci-inc,[  --with-oracleoci-inc[=DIR]      DIR is the Oracle OCI includes directory],,
	withval="$ORACLE_HOME/rdbms/demo"
)

ORACLE_OCI_INC=$withval
AC_SUBST(ORACLE_OCI_INC)

if test \! -f "$ORACLE_OCI_INC/oci.h"; then
	AC_MSG_ERROR($ORACLE_OCI_INC does not seem to be valid Oracle OCI includes directory)
fi

AC_ARG_WITH(oraclepublic-inc,[  --with-oraclepublic-inc[=DIR]      DIR is the Oracle public includes directory],,
	withval="$ORACLE_HOME/network/public"
)

ORACLE_PUBLIC_INC=$withval
AC_SUBST(ORACLE_PUBLIC_INC)

if test \! -f "$ORACLE_PUBLIC_INC/nzt.h"; then
	AC_MSG_ERROR($ORACLE_PUBLIC_INC does not seem to be valid Oracle public includes directory)
fi

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
ctype.h \
setjmp.h
)

AC_MSG_CHECKING(for BSD setenv/getenv/unsetenv)
AC_TRY_LINK(
#ifdef HAVE_STDLIB_H
#	include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#	include <string.h>
#endif
,
#define TEST_NAME "oracle-configure-test-var"
#define TEST_VALUE "oracle-configure-test-value"
int result=setenv(TEST_NAME, TEST_VALUE, 1/*overwrite*/);
char *getenv_value=getenv(TEST_NAME);
if(!getenv_value)
	return 1;
if(strcmp(getenv_value, TEST_VALUE)!=0)
	return 2;
unsetenv(TEST_NAME);
,[
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_SETENV,,library has BSD setenv/unsetenv)
],		
	AC_MSG_RESULT(no)

	AC_MSG_CHECKING(for POSIX putenv/getenv)
	AC_TRY_LINK(
	#ifdef HAVE_STDLIB_H
	#	include <stdlib.h>
	#endif
	#ifdef HAVE_STRING_H
	#	include <string.h>
	#endif
	,
	#define TEST_NAME "oracle-configure-test-var"
	#define TEST_VALUE "oracle-configure-test-value"
	int result=putenv(TEST_NAME"="TEST_VALUE);
	char *getenv_value=getenv(TEST_NAME);
	if(!getenv_value)
		return 1;
	if(strcmp(getenv_value, TEST_NAME)!=0)
		return 2;
	,[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PUTENV,,library has POSIX putenv)
	],		
		AC_MSG_RESULT(no)
		AC_MSG_ERROR("there must be some function to modify environment")
	)
)

dnl Checks for typedefs, structures, and compiler characteristics
AC_TYPE_SIZE_T

dnl Checks for library functions

dnl Output makefiles
AC_OUTPUT(Makefile)
