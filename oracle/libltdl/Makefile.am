## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = no-dependencies foreign

noinst_LTLIBRARIES = libltdlc.la

noinst_HEADERS = ltdl.h config_auto.h.in config.h
libltdlc_la_SOURCES = ltdl.c
libltdlc_la_LIBADD = $(LIBADD_DL)

## Because we do not have automatic dependency tracking:
ltdl.lo: ltdl.h config.h

EXTRA_DIST=config_fixed.h libltdl.dsp