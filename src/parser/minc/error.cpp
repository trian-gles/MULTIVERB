/* RTcmix  - Copyright (C) 2004  The RTcmix Development Team
   See ``AUTHORS'' for a list of contributors. See ``LICENSE'' for
   the license to this software and for a DISCLAIMER OF ALL WARRANTIES.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "rename.h"
#include "minc_internal.h"
#include "minc_defs.h"

#define BUFSIZE 1024

#ifndef vsnprintf
#define vsnprintf(str, sz, fmt, args)  vsprintf(str, fmt, args)
#endif

// BGG -- these in message.c.  Maybe just #include <ugens.h>?
extern void rtcmix_advise(const char *inst_name, const char *format, ...);
extern void rtcmix_warn(const char *inst_name, const char *format, ...);
extern void rterror(const char *inst_name, const char *format, ...);
extern int die(const char *inst_name, const char *format, ...);
extern "C" int yy_get_stored_lineno();

void
sys_error(const char *msg)
{
	die("parser", "%s", msg);
	throw(MincSystemError);
}

void
minc_advise(const char *msg, ...)
{
   char buf[BUFSIZE];
   va_list args;

   va_start(args, msg);
   vsnprintf(buf, BUFSIZE, msg, args);
   va_end(args);

	rtcmix_advise("parser", buf);
}

void
minc_warn(const char *msg, ...)
{
   char buf[BUFSIZE];
   va_list args;

   va_start(args, msg);
   vsnprintf(buf, BUFSIZE, msg, args);
   va_end(args);

    const char *includedFile = yy_get_current_include_filename();
    if (includedFile) {
        rtcmix_warn("parser", "%s ('%s', near line %d)", buf, includedFile, yy_get_stored_lineno());
    }
    else {
        rtcmix_warn("parser", "%s (near line %d)", buf, yy_get_stored_lineno());
    }
}

void
minc_die(const char *msg, ...)
{
   char buf[BUFSIZE];
   va_list args;

   va_start(args, msg);
   vsnprintf(buf, BUFSIZE, msg, args);
   va_end(args);

    const char *includedFile = yy_get_current_include_filename();
    if (includedFile) {
        rterror("parser", "%s ('%s', near line %d)", buf, includedFile, yy_get_stored_lineno());
    }
    else {
        rterror("parser", "%s (near line %d)", buf, yy_get_stored_lineno());
    }

	throw(MincParserError);
}

void
minc_internal_error(const char *msg, ...)
{
   char buf[BUFSIZE];
   va_list args;

   va_start(args, msg);
   vsnprintf(buf, BUFSIZE, msg, args);
   va_end(args);

    const char *includedFile = yy_get_current_include_filename();
    if (includedFile) {
        rterror("parser-program", "%s ('%s', near line %d)", buf, includedFile, yy_get_stored_lineno());
    }
    else {
        rterror("parser-program", "%s (near line %d)", buf, yy_get_stored_lineno());
    }
	throw(MincInternalError);
}

void
yyerror(const char *msg)
{
    const char *includedFile = yy_get_current_include_filename();
    if (includedFile) {
        rterror("parser-yyerror", "'%s', near line %d: %s", includedFile, yy_get_stored_lineno(), msg);
    }
    else {
        rterror("parser-yyerror", "near line %d: %s", yy_get_stored_lineno(), msg);
    }
	throw(MincParserError);
}

void
yyfatalerror(const char *msg)
{
    const char *includedFile = yy_get_current_include_filename();
    if (includedFile) {
        rterror("parser-yyfatalerror", "'%s', near line %d: %s", includedFile, yy_get_stored_lineno(), msg);
    }
    else {
        rterror("parser-yyfatalerror", "near line %d: %s", yy_get_stored_lineno(), msg);
    }
    throw(MincParserError);
}

