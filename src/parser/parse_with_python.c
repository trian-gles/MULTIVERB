/* RTcmix  - Copyright (C) 2001  The RTcmix Development Team
   See ``AUTHORS'' for a list of contributors. See ``LICENSE'' for
   the license to this software and for a DISCLAIMER OF ALL WARRANTIES.
*/

/* Functions for managing embedded Python parser.    -JGG, 12-Feb-01 */

#include <assert.h>
#include <Python.h>
#include "rtcmix_parse.h"

static FILE *_script = NULL;
static char *_script_name = NULL;

#ifdef PYEXT_INIT
extern void initrtcmix(void);    /* defined in rtcmixmodule.cpp */
#endif

/* ---------------------------------------------------------- parse_score --- */
int
parse_score(int argc, char *argv[])
{
   int   status, xargc;
   char  *xargv[MAXARGS + 2];

   assert(argc <= MAXARGS);

   Py_SetProgramName(argv[0]);

   /* Init Python interpreter.  If this fails, it won't return. */
   Py_Initialize();

   /* Define sys.argv in Python. */
   PySys_SetArgv(argc, argv);

   /* If we're linking statically to extension module, init it */
#ifdef PYEXT_INIT
   initrtcmix();
#endif

   if (_script == NULL)
      _script = stdin;
   /* Otherwise, <_script> will have been set by use_script_file. */

   /* Run the Python interpreter. */
   PyRun_AnyFile(_script, _script_name);

   /* Kill interpreter, so that it won't trap cntl-C while insts play.
      Actually, it turns out that this doesn't help, at least for 
      Python 2.x, so we have to reinstall our SIGINT handler in main().
   */
   Py_Finalize();                /* any errors ignored internally */

   return 0;
}


/* ------------------------------------------------------ use_script_file --- */
/* Parse file <fname> instead of stdin. */
void
use_script_file(char *fname)
{
   _script = fopen(fname, "r");
   if (_script == NULL) {
      fprintf(stderr, "Can't open %s\n", fname);
      return;
   }
   _script_name = fname;
   printf("Using file %s\n", fname);
}


/* ------------------------------------------------------- destroy_parser --- */
void
destroy_parser()
{
   /* nothing to do (see Py_Finalize() above) */
}

