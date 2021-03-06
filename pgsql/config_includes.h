/** @file
	Parser PgSQL driver: includes all Configure-d headers

	Copyright (c) 2001-2019 Art. Lebedev Studio (http://www.artlebedev.com)

	Author: Alexander Petrosyan <paf@design.ru> (http://design.ru/paf)

	$Id: config_includes.h,v 1.8 2019/11/30 22:11:11 moko Exp $


	when used Configure [HAVE_CONFIG_H] it uses defines from Configure,
	fixed otherwise.
*/

#if HAVE_CONFIG_H
#	include "config_auto.h"
#else
#	include "config_fixed.h"
#endif

#ifdef HAVE_STRING_H
#	include <string.h>
#endif

#ifdef HAVE_STDIO_H
#	include <stdio.h>
#endif

#ifdef HAVE_STDLIB_H
#	include <stdlib.h>
#endif

#ifdef HAVE_SETJMP_H
#	include <setjmp.h>
#endif

#ifdef HAVE_CTYPE_H
#	include <ctype.h>
#endif
