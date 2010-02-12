/*-------------------------------------------------------------------------
 * Copyright (c) 1996 - 2009   The Regents of the University of California.
 *                      All rights reserved.
 *
 * This work was produced at the University of California,  Lawrence Liver-
 * more National Laboratory  (UC  LLNL)  under  contract no.  W-7405-ENG-48
 * (Contract 48)  between the U.S.  Department  of  Energy  (DOE)  and  The
 * Regents of the University of California  (University) for  the operation
 * of UC LLNL.   Copyright is reserved  to the University  for purposes  of
 * controlled dissemination, commercialization through formal licensing, or
 * other disposition under terms of Contract 48;  DOE policies, regulations
 * and orders; and U.S. statutes.  The rights of the Federal Government are
 * reserved under  Contract 48 subject  to the restrictions  agreed upon by
 * DOE and University.
 *
 *                            DISCLAIMER
 *
 * This software was prepared as an account of work  sponsored by an agency
 * of the United States  Government.  Neither the United  States Government
 * nor the University of  California nor any  of their employees, makes any
 * warranty, express or implied, or assumes  any liability or responsiblity
 * for the accuracy, completeness, or usefullness of any information, appa-
 * ratus,  product,  or process disclosed, or represents that its use would
 * not infringe  privately owned rights.   Reference herein to any specific
 * commercial products, process, or service by trade name, trademark, manu-
 * facturer, or otherwise, does not necessarily constitute or imply its en-
 * dorsement,  recommendation,  or favoring by the United States Government
 * or the University of California.   The views and opinions of authors ex-
 * pressed herein do not necessarily  state or reflect those  of the United
 * States Government or the University of California, and shall not be used
 * for advertising or product endorsement purposes.
 *
 *-------------------------------------------------------------------------
 *
 * Created:             browser.c
 *                      Dec  4 1996
 *                      Robb Matzke <matzke@viper.llnl.gov>
 *
 * Purpose:             A better browser
 *
 * Modifications:
 *
 *      Sean Ahern, Fri Feb 28 14:13:29 PST 1997
 *      Added checks for the readline library.
 *
 *      Sean Ahern, Tue Mar  3 16:00:09 PST 1998
 *      Added a USAGE message, telling the user about flags to browser.
 *    
 *      Jeremy Meredith, Sept 21 1998
 *      Added support for multi-species object.
 *
 *      Robb Matzke, 2000-05-31
 *      Modularized the command-line parsing.
 *
 *      Thomas Treadway, Thu Jun  8 16:56:35 PDT 2006
 *      Modified readline definitions to support new configure macro.
 *-------------------------------------------------------------------------
 */
#include "config.h"     /*MeshTV configuration record*/

#include <silo.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "browser.h"
#include <ctype.h>
#include <pwd.h>
#include <math.h>
#ifdef HAVE_LIBREADLINE
#  if defined(HAVE_READLINE_READLINE_H)
#    include <readline/readline.h>
#  elif defined(HAVE_READLINE_H)
#    include <readline.h>
#  else /* !defined(HAVE_READLINE_H) */
extern char *readline ();
#  endif /* !defined(HAVE_READLINE_H) */
char *cmdline = NULL;
#else /* !defined(HAVE_READLINE_READLINE_H) */
  /* no readline */
#endif /* HAVE_LIBREADLINE */

#ifdef HAVE_READLINE_HISTORY
#  if defined(HAVE_READLINE_HISTORY_H)
#    include <readline/history.h>
#  elif defined(HAVE_HISTORY_H)
#    include <history.h>
#  else /* !defined(HAVE_HISTORY_H) */
extern void add_history ();
extern int write_history ();
extern int read_history ();
#  endif /* defined(HAVE_READLINE_HISTORY_H) */
  /* no history */
#endif /* HAVE_READLINE_HISTORY */
#include <signal.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif /* HAVE_SYS_STAT_H */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

const char *arg0;               /*argv[0]                               */
int     Verbosity=1;            /*0=none, 1=normal, 2=more              */
char    HistoryFile[1024];      /*command history file name             */
size_t  Trapped_FPE;            /*how many SIGFPE's were raised?        */
size_t  Trapped_WINCH;          /*how many SIGWINCH's were raised?      */
switches_t *Switches=NULL;      /*command-line switches                 */

char    *ObjTypeName[BROWSER_NOBJTYPES] = {
   "curve", "multimesh", "multivar", "multimat", "multispecies", "qmesh", 
   "qvar", "ucdmesh", "ucdvar", "ptmesh", "ptvar", "mat", "matspecies", "var",
   "obj", "dir", "array", "defvars", "csgmesh", "csgvar", "multimeshadj",
   "mrgtree", "groupelmap", "mrgvar"
};

/*
 * Externals for the ale3d and debug filter registration.
 */
extern int       f_ale3d_Open ();
extern int       f_debug_Open ();

/*---------------------------------------------------------------------------
 * Purpose:     Print a usage message to the standard error stream.
 *
 * Programmer:  Robb Matzke
 *              Wednesday, May 31, 2000
 *
 * Modifications:
 *---------------------------------------------------------------------------
 */
void
usage(void)
{
    switch_usage(Switches, arg0, NULL);
}

/*-------------------------------------------------------------------------
 * Function:    pl
 *
 * Purpose:     Debugger function to print an object to the standard
 *              output stream followed by a return.
 *
 * Return:      Success:        SELF
 *
 *              Failure:        SELF
 *
 * Programmer:  Robb Matzke
 *              matzke@viper.llnl.gov
 *              Dec  4 1996
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static obj_t
pl (obj_t self) {

   extern int   DebugPrinting;

   DebugPrinting++;
   out_reset (OUT_STDERR);
   out_printf (OUT_STDERR, "Object: ");
   obj_print (self, OUT_STDERR);
   out_nl (OUT_STDERR);
   --DebugPrinting;
   return self;
}


/*-------------------------------------------------------------------------
 * Function:    sort_toc_by_name
 *
 * Purpose:     Compares two table of contents entries.  This sort function
 *              sorts first by name then by object type.
 *
 * Return:      Success:        n<0, n==0, n>0 like strcmp.
 *
 *              Failure:        never fails
 *
 * Programmer:  Robb Matzke
 *              robb@maya.nuance.mdn.com
 *              Feb  6 1997
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
int
sort_toc_by_name (toc_t *a, toc_t *b) {

   int          cmp;

   cmp = strcmp (a->name, b->name);
   if (cmp) return cmp;

   if (a->type == b->type) return 0;
   return a->type < b->type ? -1 : 1;
}


/*-------------------------------------------------------------------------
 * Function:    sort_toc_by_type
 *
 * Purpose:     Compares two table of contents entries.  This sort function
 *              sorts first by type then by name.
 *
 * Return:      Success:        n<0, n==0, n>0 like strcmp().
 *
 *              Failure:        never fails
 *
 * Programmer:  Robb Matzke
 *              robb@maya.nuance.mdn.com
 *              Feb  6 1997
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
int
sort_toc_by_type (toc_t *a, toc_t *b) {

   if (a->type!=b->type) return a->type < b->type ? -1 : 1;
   return strcmp (a->name, b->name);
}

/*-------------------------------------------------------------------------
 * Function:    browser_DBGetToc
 *
 * Purpose:     Reads a table of contents from the current directory
 *              and builds an easier-to-use data structure.
 *
 * Return:      Success:        A browser table of contents.
 *
 *              Failure:        NULL, including when the table of contents
 *                              is empty.
 *
 * Programmer:  Robb Matzke
 *              robb@maya.nuance.mdn.com
 *              Feb  6 1997
 *
 * Modifications:
 *    Jeremy Meredith, Sept 21 1998
 *    Added multimatspecies to TOC.
 *
 *    Jeremy Meredith, Mon Aug 16 13:30:36 PDT 1999
 *    I replaced strdup with safe_strdup.
 *
 *    Robb Matzke, Fri May 19 13:32:08 EDT 2000
 *    Avoid calls to malloc(0) since the result isn't defined by Posix.
 *-------------------------------------------------------------------------
 */
toc_t *
browser_DBGetToc (DBfile *file, int *nentries, int (*sorter)(toc_t*,toc_t*)) {

   int          total;                  /*total number of objects       */
   toc_t        *retval=NULL;           /*table of contents             */
   int          i, at=0;
   DBtoc        *toc;

   toc = DBGetToc (file);
   total = toc->ncurve + toc->nmultimesh + toc->nmultivar +
           toc->nmultimat + toc->nmultimatspecies + toc->nqmesh + 
           toc->nqvar + toc->nucdmesh + toc->nucdvar + toc->nptmesh +
           toc->nptvar + toc->nmat + toc->nmatspecies + toc->nvar + 
           toc->nobj + toc->ndir + toc->narrays + toc->ndefvars +
           toc->ncsgmesh + toc->ncsgvar + toc->nmultimeshadj +
           toc->nmrgtrees + toc->ngroupelmaps + toc->nmrgvars;
   if (total) retval = malloc (total * sizeof(toc_t));

   /*
    * Load the various types of objects into the new structure.
    */
   for (i=0; i<toc->ndefvars; i++,at++) {
      retval[at].name = safe_strdup (toc->defvars_names[i]);
      retval[at].type = BROWSER_DB_DEFVARS;
   }

   for (i=0; i<toc->ncsgmesh; i++,at++) {
      retval[at].name = safe_strdup (toc->csgmesh_names[i]);
      retval[at].type = BROWSER_DB_CSGMESH;
   }

   for (i=0; i<toc->ncsgvar; i++,at++) {
      retval[at].name = safe_strdup (toc->csgvar_names[i]);
      retval[at].type = BROWSER_DB_CSGVAR;
   }

   for (i=0; i<toc->ncurve; i++,at++) {
      retval[at].name = safe_strdup (toc->curve_names[i]);
      retval[at].type = BROWSER_DB_CURVE;
   }

   for (i=0; i<toc->nmultimesh; i++,at++) {
      retval[at].name = safe_strdup (toc->multimesh_names[i]);
      retval[at].type = BROWSER_DB_MULTIMESH;
   }

   for (i=0; i<toc->nmultimeshadj; i++,at++) {
      retval[at].name = safe_strdup (toc->multimeshadj_names[i]);
      retval[at].type = BROWSER_DB_MULTIMESHADJ;
   }

   for (i=0; i<toc->nmultivar; i++,at++) {
      retval[at].name = safe_strdup (toc->multivar_names[i]);
      retval[at].type = BROWSER_DB_MULTIVAR;
   }

   for (i=0; i<toc->nmultimat; i++,at++) {
      retval[at].name = safe_strdup (toc->multimat_names[i]);
      retval[at].type = BROWSER_DB_MULTIMAT;
   }

   for (i=0; i<toc->nmultimatspecies; i++,at++) {
      retval[at].name = safe_strdup (toc->multimatspecies_names[i]);
      retval[at].type = BROWSER_DB_MULTIMATSPECIES;
   }

   for (i=0; i<toc->nqmesh; i++,at++) {
      retval[at].name = safe_strdup (toc->qmesh_names[i]);
      retval[at].type = BROWSER_DB_QMESH;
   }

   for (i=0; i<toc->nqvar; i++,at++) {
      retval[at].name = safe_strdup (toc->qvar_names[i]);
      retval[at].type = BROWSER_DB_QVAR;
   }

   for (i=0; i<toc->nucdmesh; i++,at++) {
      retval[at].name = safe_strdup (toc->ucdmesh_names[i]);
      retval[at].type = BROWSER_DB_UCDMESH;
   }

   for (i=0; i<toc->nucdvar; i++,at++) {
      retval[at].name = safe_strdup (toc->ucdvar_names[i]);
      retval[at].type = BROWSER_DB_UCDVAR;
   }

   for (i=0; i<toc->nptmesh; i++,at++) {
      retval[at].name = safe_strdup (toc->ptmesh_names[i]);
      retval[at].type = BROWSER_DB_PTMESH;
   }

   for (i=0; i<toc->nptvar; i++,at++) {
      retval[at].name = safe_strdup (toc->ptvar_names[i]);
      retval[at].type = BROWSER_DB_PTVAR;
   }

   for (i=0; i<toc->nmat; i++,at++) {
      retval[at].name = safe_strdup (toc->mat_names[i]);
      retval[at].type = BROWSER_DB_MAT;
   }

   for (i=0; i<toc->nmatspecies; i++,at++) {
      retval[at].name = safe_strdup (toc->matspecies_names[i]);
      retval[at].type = BROWSER_DB_MATSPECIES;
   }

   for (i=0; i<toc->nvar; i++,at++) {
      retval[at].name = safe_strdup (toc->var_names[i]);
      retval[at].type = BROWSER_DB_VAR;
   }

   for (i=0; i<toc->nobj; i++,at++) {
      retval[at].name = safe_strdup (toc->obj_names[i]);
      retval[at].type = BROWSER_DB_OBJ;
   }

   for (i=0; i<toc->ndir; i++,at++) {
      retval[at].name = safe_strdup (toc->dir_names[i]);
      retval[at].type = BROWSER_DB_DIR;
   }

   for (i=0; i<toc->narrays; i++,at++) {
      retval[at].name = safe_strdup (toc->array_names[i]);
      retval[at].type = BROWSER_DB_ARRAY;
   }

   for (i=0; i<toc->nmrgtrees; i++,at++) {
      retval[at].name = safe_strdup (toc->mrgtree_names[i]);
      retval[at].type = BROWSER_DB_MRGTREE;
   }

   for (i=0; i<toc->ngroupelmaps; i++,at++) {
      retval[at].name = safe_strdup (toc->groupelmap_names[i]);
      retval[at].type = BROWSER_DB_GROUPELMAP;
   }

   for (i=0; i<toc->nmrgvars; i++,at++) {
      retval[at].name = safe_strdup (toc->mrgvar_names[i]);
      retval[at].type = BROWSER_DB_MRGVAR;
   }

   assert (at==total);

   /*
    * Sort by name.
    */
   qsort (retval, total, sizeof(toc_t),
          (int(*)(const void*,const void*))sorter);
   if (nentries) *nentries = at;
   return retval;
}

/*-------------------------------------------------------------------------
 * Function:    browser_rl_obj_generator
 *
 * Purpose:     Returns completions for TEXT.  STATE is zero the first time
 *              this function is called for a particular completion.  TEXT
 *              is the (partial) name of the object being completed.
 *
 * Return:      Success:        Ptr to one of the possible completions.
 *                              When STATE==0 the completion list is
 *                              regenerated.
 *
 *              Failure:        NULL when all completions have been
 *                              returned.
 *
 * Programmer:  Robb Matzke
 *              matzke@viper.llnl.gov
 *              Feb  9 1997
 *
 * Modifications:
 *
 *      Sean Ahern, Fri Feb 28 14:13:50 PST 1997
 *      Added a check for the readline library.
 *
 *      Jeremy Meredith, Mon Aug 16 13:30:36 PDT 1999
 *      I replaced strdup with safe_strdup.
 *
 *-------------------------------------------------------------------------
 */
#if defined(HAVE_READLINE_READLINE_H) && defined(HAVE_LIBREADLINE)
static char *
browser_rl_obj_generator (char *text, int state) {

   obj_t        f1, val;
   int          i, n;
   char         *s, *slash, cwd[1024];
   DBfile       *file;

   static toc_t *toc=NULL;
   static int   nentries, current, length;
   static char  *dir=NULL;

   if (0==state) {
      /*
       * Free any previous table of contents.
       */
      if (toc) {
         for (i=0; i<nentries; i++) {
            if (toc[i].name) free (toc[i].name);
         }
         free (toc);
         if (dir) free (dir);
         toc = NULL;
      }

      /*
       * Split the name into a directory and base name.
       */
      if ((slash=strchr(text, '/'))) {
         if (slash==text) {
            dir = safe_strdup ("/");
         } else {
            n = slash - text;
            dir = malloc (n+1);
            strncpy (dir, text, n);
            dir[n] = '\0';
         }
         while ('/'==*slash) slash++;
      } else {
         dir = NULL;
      }

      /*
       * Get the file
       */
      f1 = obj_new (C_SYM, "$1");
      val = sym_vboundp (f1);
      f1 = obj_dest (f1);
      if (!val) return NULL;                    /*no completions*/
      if (NULL==(file=file_file(val))) {
         val = obj_dest (val);
         return NULL;                           /*$1 is not a file      */
      }
      val = obj_dest (val);

      /*
       * Change to the directory and read the table of contents.
       */
      DBShowErrors (DB_SUSPEND, NULL);
      if (dir) {
         DBGetDir (file, cwd);
         if (DBSetDir (file, dir)<0) {
            free (dir);
            dir = NULL;
            return NULL;
         }
      }
      toc = browser_DBGetToc (file, &nentries, sort_toc_by_name);
      if (dir) DBSetDir (file, cwd);
      DBShowErrors (DB_RESUME, NULL);

      /*
       * Replace each name in the table of contents with the full
       * name.
       */
      if (dir) {
         n = strlen (dir) + (strcmp(dir,"/") ? 1 : 0);
         for (i=0; i<nentries; i++) {
            s = malloc (n + strlen(toc[i].name) + 1);
            sprintf (s, "%s%s%s", dir, strcmp(dir,"/")?"/":"", toc[i].name);
            free (toc[i].name);
            toc[i].name = s;
         }
      }

      current = 0;
      length = strlen (text);
   }

   /*
    * Return the next entry that matches the partial base name.
    */
   for (/*void*/; current<nentries; current++) {
      if (!strncmp(toc[current].name, text, length)) {
         s = toc[current].name;
         toc[current].name = NULL;
         current++;
         return s;
      }
   }

   return NULL;
}
#endif /*HAVE_READLINE_READLINE_H && HAVE_LIBREADLINE */

/*-------------------------------------------------------------------------
 * Function:    different
 *
 * Purpose:     Determines if A and B are same or different based on an
 *              absolute tolerance and relative tolerance.  A and B differ
 *              if and only if
 *
 *                      | A-B | > ABSTOL
 *
 *              or
 *
 *                      2 | A-B |
 *                      ---------  > RELTOL
 *                       | A+B |
 *
 *              If ABSTOL or RELTOL is negative then the corresponding
 *              test is not performed.  If both are negative then this
 *              function degenerates to a `!=' operator.
 *
 * Return:      Success:        0 if same, 1 if different.
 *
 *              Failure:        never fails
 *
 * Programmer:  Robb Matzke
 *              matzke@viper.llnl.gov
 *              Feb  6 1997
 *
 * Modifications:
 *
 *  Mark C. Miller, Wed Nov 11 22:18:17 PST 2009
 *  Added suppot for alternate relative diff option.
 *-------------------------------------------------------------------------
 */
int
different (double a, double b, double abstol, double reltol,
    double reltol_eps) {

   double       num, den;

   /*
    * Now the |A-B| but make sure it doesn't overflow which can only
    * happen if one is negative and the other is positive.
    */
   if (abstol>0) {
      if ((a<0 && b>0) || (b<0 && a>0)) {
         if (fabs (a/2 - b/2) > abstol/2) return 1;
      } else {
         if (fabs(a-b) > abstol) return 1;
      }
   }

   /*
    * First, see if we should use the alternate diff.
    * check |A-B|/(|A|+|B|+EPS) in a way that won't overflow.
    */
   if (reltol_eps >= 0)
   {
      if ((a<0 && b>0) || (b<0 && a>0)) {
         num = fabs (a/2 - b/2);
         den = fabs (a/2) + fabs(b/2) + reltol_eps/2;
         reltol /= 2;
      } else {
         num = fabs (a - b);
         den = fabs (a) + fabs(b) + reltol_eps;
      }
      if (0.0==den && num) return 1;
      if (num/den > reltol) return 1;
   }

   /*
    * Now check 2|A-B|/|A+B| in a way that won't overflow.
    */
   if (reltol>0) {
      if ((a<0 && b>0) || (b<0 && a>0)) {
         num = fabs (a/2 - b/2);
         den = fabs (a/2 + b/2);
         reltol /= 2;
      } else {
         num = fabs (a - b);
         den = fabs (a/2 + b/2);
      }
      if (0.0==den && num) return 1;
      if (num/den > reltol) return 1;
   }

   /*
    * If all tests tried succeeded then the numbers are equal.
    */
   if (abstol>0 || reltol>0 || reltol_eps>=0) return 0;

   /*
    * Otherwise do a normal exact comparison.
    */
   return a!=b;
}

/*-------------------------------------------------------------------------
 * Function:    differentll
 *
 * Purpose:     Implement above difference function for long long type. 
 *
 * Programmer:  Mark C. Miller, Mon Dec  7 07:05:39 PST 2009
 *
 * Modifications:
 *   Mark C. Miller, Mon Dec  7 09:50:19 PST 2009
 *   Change conditional compilation logic to compile this routine
 *   whenever a double is NOT sufficient to hold full precision of long
 *   or long long.
 *
 *   Mark C. Miller, Mon Jan 11 16:20:16 PST 2010
 *   Made it compiled UNconditionally.
 *-------------------------------------------------------------------------
 */
#define FABS(A) ((A)<0?-(A):(A))
int
differentll (long long a, long long b, double abstol, double reltol,
    double reltol_eps) {

   long long num, den;

   /*
    * Now the |A-B| but make sure it doesn't overflow which can only
    * happen if one is negative and the other is positive.
    */
   if (abstol>0) {
      if ((a<0 && b>0) || (b<0 && a>0)) {
         if (FABS(a/2 - b/2) > abstol/2) return 1;
      } else {
         if (FABS(a-b) > abstol) return 1;
      }
   }

   /*
    * First, see if we should use the alternate diff.
    * check |A-B|/(|A|+|B|+EPS) in a way that won't overflow.
    */
   if (reltol_eps >= 0)
   {
      if ((a<0 && b>0) || (b<0 && a>0)) {
         num = FABS (a/2 - b/2);
         den = FABS (a/2) + FABS(b/2) + reltol_eps/2;
         reltol /= 2;
      } else {
         num = FABS (a - b);
         den = FABS (a) + FABS(b) + reltol_eps;
      }
      if (0.0==den && num) return 1;
      if (num/den > reltol) return 1;
   }

   /*
    * Now check 2|A-B|/|A+B| in a way that won't overflow.
    */
   if (reltol>0) {
      if ((a<0 && b>0) || (b<0 && a>0)) {
         num = FABS (a/2 - b/2);
         den = FABS (a/2 + b/2);
         reltol /= 2;
      } else {
         num = FABS (a - b);
         den = FABS (a/2 + b/2);
      }
      if (0.0==den && num) return 1;
      if (num/den > reltol) return 1;
   }

   /*
    * If all tests tried succeeded then the numbers are equal.
    */
   if (abstol>0 || reltol>0 || reltol_eps>=0) return 0;

   /*
    * Otherwise do a normal exact comparison.
    */
   return a!=b;
}

/*-------------------------------------------------------------------------
 * Function:    set_diff
 *
 * Purpose:     Sets all differencing symbols to the same value.
 *
 * Return:      void
 *
 * Programmer:  Robb Matzke
 *              robb@maya.nuance.mdn.com
 *              Feb 25 1997
 *
 * Modifications:
 *
 *      Robb Matzke, 2 Sep 1997
 *      Added initialization of the `int8' differencing parameters.
 *
 *      Mark C. Miller, Mon Jan 11 16:20:39 PST 2010
 *      Added initialization for long long diff params.
 *-------------------------------------------------------------------------
 */
static void
set_diff (const char *suffix, const char *d) {

   char         tmp[64];
   obj_t        name=NIL;

   sprintf (tmp, "$diff_int8_%s", suffix);
   name = obj_new (C_SYM, tmp);
   sym_vbind (name, obj_new (C_NUM, d));
   name = obj_dest (name);

   sprintf (tmp, "$diff_short_%s", suffix);
   name = obj_new (C_SYM, tmp);
   sym_vbind (name, obj_new (C_NUM, d));
   name = obj_dest (name);

   sprintf (tmp, "$diff_int_%s", suffix);
   name = obj_new (C_SYM, tmp);
   sym_vbind (name, obj_new (C_NUM, d));
   name = obj_dest (name);

   sprintf (tmp, "$diff_long_%s", suffix);
   name = obj_new (C_SYM, tmp);
   sym_vbind (name, obj_new (C_NUM, d));
   name = obj_dest (name);

   sprintf (tmp, "$diff_float_%s", suffix);
   name = obj_new (C_SYM, tmp);
   sym_vbind (name, obj_new (C_NUM, d));
   name = obj_dest (name);

   sprintf (tmp, "$diff_double_%s", suffix);
   name = obj_new (C_SYM, tmp);
   sym_vbind (name, obj_new (C_NUM, d));
   name = obj_dest (name);

   sprintf (tmp, "$diff_llong_%s", suffix);
   name = obj_new (C_SYM, tmp);
   sym_vbind (name, obj_new (C_NUM, d));
   name = obj_dest (name);

}

static double 
browser_version()
{
    return strtod(DBVersion(), 0);
}

/*-------------------------------------------------------------------------
 * Function:    check_version
 *
 * Purpose:     Checks the version number specified in the initialization
 *              file to make sure it's up to date with respect to the
 *              browser.  If the browser is at version N (an integer) then
 *              the initialization file version must be a real number between
 *              N (inclusive) and N+1 (exclusive).
 *
 * Return:      Success:        0 if version is OK
 *
 *              Failure:        -1
 *
 * Programmer:  Robb Matzke
 *              robb@maya.nuance.mdn.com
 *              Apr  3 1997
 *
 * Modifications:
 *-------------------------------------------------------------------------
 */
static int
check_version (const char *filename) {

   obj_t        sym, val;
   double       vers;
   struct stat  sb;

   /*
    * If the startup file is empty then don't print any warnings.
    */
   if (stat (filename, &sb)>=0 && 0==sb.st_size) return 0;

   sym = obj_new (C_SYM, "$browser_version");
   val = sym_vboundp (sym);
   sym = obj_dest (sym);

   if (!val) {
      out_errorn ("check_version: The initialization file `%s' seems to "
                  "be lacking version information and may be out of date. "
                  "Please update your initialization file or add the "
                  "statement `$browser_version=%3.1f' after checking that "
                  "the file contains valid initialization statements. Type "
                  "`help' for the latest documentation.  Or use the `-f FILE' "
                  "command-line option to specify some other startup file or "
                  "say `-f /dev/null' for no file.",
                  filename, browser_version());
      return -1;
   }

   if (!num_isfp(val)) {
      out_errorn ("check_version: The initialization file `%s' contains an "
                  "invalid version number (it should be an integer or "
                  "floating-point constant).  Type `$browser_version' to "
                  "see the version specified in the file.", filename);
      val = obj_dest (val);
      return -1;
   }

   vers = num_fp (val);
   val = obj_dest (val);

   if (vers<browser_version()) {
      out_errorn ("check_version: The initialization file `%s' was written "
                  "for version %3.1f of the browser (the current browser version "
                  "is %3.1f).  Please obtain the latest version of that file "
                  "or verify that the commands in that file still apply "
                  "to this version of the browser.  Or use the `-f FILE' "
                  "command-line option to specify some other startup file or "
                  "say `-f /dev/null' for no file.",
                  filename, vers, browser_version());
      return -1;
   }

   if (vers>=browser_version()+1) {
      out_errorn ("check_version: Based on the contents of the initialization "
                  "file `%s', you appear to be running an old version of the "
                  "browser.  The file was written for version %3.1f, but this "
                  "is browser version %3.1f.  Or use the `-f FILE' command-line "
                  "option to specify some other startup file or say `-f "
                  "/dev/null' for no file.",
                  filename, vers, browser_version());
      return -1;
   }

   return 0;
}

/*---------------------------------------------------------------------------
 * Purpose:     Signal handler for SIGFPE.
 *
 * Programmer:  Robb Matzke
 *              Friday, June  2, 2000
 *
 * Modifications:
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static void
sigfpe_handler(int signo)
{
    Trapped_FPE++;
}

/*---------------------------------------------------------------------------
 * Purpose:     Signal handler for SIGWINCH
 *
 * Programmer:  Robb Matzke
 *              Tuesday, June  6, 2000
 *
 * Modifications:
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static void
sigwinch_handler(int signo)
{
    Trapped_WINCH++;
}

/*-------------------------------------------------------------------------
 * Function:    trapped_fpe
 *
 * Purpose:     If the SIGFPE counter is positive then tell the user that
 *              NaN values might appear as zeros.
 *
 * Return:      void
 *
 * Programmer:  Robb Matzke
 *              robb@maya.nuance.com
 *              Jul 29 1997
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static void
trapped_fpe(void)
{
    static int  ncalls=0;
    
    if (Trapped_FPE) {
        if (Verbosity>=2) {
            /* Tell the user every time a prompt is issued */
            out_info("%lu floating point exception%s generated in last output",
                     (unsigned long)Trapped_FPE, 1==Trapped_FPE?"":"s");
            Trapped_FPE = 0;
        } else if (Verbosity>=1 && 0==ncalls++) {
            /* Tell the user just once */
            out_info("This system generates SIGFPE when attempting to display "
                     "invalid floating point data. Such data may appear as "
                     "zeros instead of \"NaN\"");
        }
    }
}

/*---------------------------------------------------------------------------
 * Purpose:     Perform operations that must be done each iteration
 *              through the read-eval-print loop.
 *
 * Programmer:  Robb Matzke
 *              Tuesday, June  6, 2000
 *
 * Modifications:
 *---------------------------------------------------------------------------
 */
static void
rep_update(void)
{
    int         i;

    /* Check for floating-point exceptions */
    trapped_fpe();

    /* Adjust output margins */
    if (Trapped_WINCH) {
        char tmp[32];
        Trapped_WINCH=0;
        out_init_size();
        sprintf(tmp, "%d", OUT_NROWS);
        sym_bi_set("height", tmp, NULL, NULL);
        sprintf(tmp, "%d", OUT_NCOLS);
        sym_bi_set("width", tmp, NULL, NULL);
    } else {
        if ((i=sym_bi_true("height")) && i>0) {
            OUT_NROWS = i;
        } else {
            sym_bi_set("height", "0", NULL, NULL);
            OUT_NROWS = 0; /*no paging*/
        }
        if ((i=sym_bi_true("width")) && i>0) {
            OUT_NCOLS = i;
        } else {
            sym_bi_set("width", "80", NULL, NULL);
            OUT_NCOLS = 80; /*default*/
        }
    }

    /* Verbosity */
    Verbosity = sym_bi_true("verbosity");
}

/*---------------------------------------------------------------------------
 * Purpose:     Callback for the `--version' command-line switch. It
 *              prints the version number one time.
 *
 * Return:      Zero
 *
 * Programmer:  Robb Matzke
 *              Wednesday, May 31, 2000
 *
 * Modifications:
 *
 *   Mark C. Miller, Tue Mar 10 15:42:31 PDT 2009
 *   Changed to report version of the Silo library browser is linked with.
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static int
process_sw_version(switch_t *sw, const char *argv, const char *value)
{
    static int  ncalls=0;

    if (0==ncalls++) {
        out_info("This browser is linked with version %s of the Silo library",
                 DBVersion());
    }
    return 0;
}

/*---------------------------------------------------------------------------
 * Purpose:     Callback for the `--help' command-line switch. It prints a
 *              help message one time.
 *
 * Return:      Zero
 *
 * Programmer:  Robb Matzke
 *              Wednesday, May 31, 2000
 *
 * Modifications:
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static int
process_sw_help(switch_t *sw, const char *argv, const char *value)
{
    static int  ncalls=0;

    if (0==ncalls++) usage();
    return 0;
}

/*---------------------------------------------------------------------------
 * Purpose:     Handles the `--verbose' and `--quiet' command-line switches.
 *
 * Return:      0
 *
 * Programmer:  Robb Matzke
 *              Wednesday, May 31, 2000
 *
 * Modifications:
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static int
process_sw_verbose(switch_t *sw, const char *argv, const char *value)
{
    char        buf[16];

    /* Set browser Verbosity */
    if (!strcmp(sw->long_name, "--verbose")) {
        Verbosity=2;
    } else {
        Verbosity=0;
    }

    /* Set internal variable */
    sprintf(buf, "%d", Verbosity);
    sym_bi_set("verbosity", buf, NULL, NULL);
    
    return 0;
}

/*---------------------------------------------------------------------------
 * Purpose:     Callback for `--eval' command-line switch.
 *
 * Return:      Zero on success, negative on failure
 *
 * Programmer:  Robb Matzke
 *              Wednesday, May 31, 2000
 *
 * Modifications:
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static int
process_sw_eval(switch_t *sw, const char *argv, const char *value)
{
    strlist_t   *eval_list = (strlist_t*)(sw->info);

    if (eval_list->nused>=NELMTS(eval_list->value)) {
        out_errorn("too many `%s' and/or `%s' switches",
                   sw->long_name, sw->short_name);
        return -1;
    }

    eval_list->value[eval_list->nused++] = safe_strdup(value);
    return 0;
}
    
/*---------------------------------------------------------------------------
 * Purpose:     Callback for `--exclude' command-line switch.
 *
 * Return:      Zero on success, negative on failure
 *
 * Programmer:  Robb Matzke
 *              Wednesday, May 31, 2000
 *
 * Modifications:
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static int
process_sw_exclude(switch_t *sw, const char *argv, const char *value)
{
    strlist_t   *exclude_list = (strlist_t*)(sw->info);
    char        *str = safe_strdup(value);
    char        *t, *name;

    for (t=str; (name=strtok(t, ", ")); t=NULL) {
        /* Check for overflow */
        if (exclude_list->nused>=NELMTS(exclude_list->value)) {
            out_errorn("too many `%s' and/or `%s' switches",
                       sw->long_name, sw->short_name);
            return -1;
        }

        /* Add name to list */
        exclude_list->value[exclude_list->nused++] = safe_strdup(name);
    }
    if (str) free(str);
    return 0;
}

/*---------------------------------------------------------------------------
 * Purpose:     Handle split-vfd switch
 *
 * Programmer:  Mark C. Miller, Fri Feb 12 08:27:52 PST 2010
 * 
 * Note: we can get away with re-using process_sw_exclude here because
 * that function makes no assumptions about which switch it is processing.
 * Instead, it gets that info from the sw->info member.
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static int
process_sw_split_vfd(switch_t *sw, const char *argv, const char *value)
{
    strlist_t   *split_vfd_list = (strlist_t*)(sw->info);
    int retval = process_sw_exclude(sw, argv, value);
    if (split_vfd_list->nused % 2)
    {
        out_errorn("split vfd extensions must be specified in pairs of "
            "strings, meta-extension followed by raw-extension.");
        retval = -1;
    }
    return retval;
}
    
/*---------------------------------------------------------------------------
 * Purpose:     Process command-line switches.
 *
 * Programmer:  Robb Matzke
 *              Wednesday, May 31, 2000
 *
 * Modifications:
 *              Robb Matzke, 2000-10-19
 *              Set $obase from `--obase' argument.
 *
 *   Mark C. Miller, Wed Sep  2 16:46:46 PDT 2009
 *   Added the 'pass' argument so we can skip resetting of $lowlevel on 
 *   passes other than the first.
 *
 *  Mark C. Miller, Wed Nov 11 22:18:17 PST 2009
 *  Added suppot for alternate relative diff option using epsilon switch.
 *---------------------------------------------------------------------------
 */
static void
process_switches(switches_t *switches, int pass)
{
    switch_t    *sw;
    char        tmp[32], *s, *t, *word;
    int         argc, i;
    obj_t       argv[25], sym, val;

    if ((sw=switch_find(switches, "--absolute")) && sw->seen) {
        set_diff("abs", sw->lexeme);
    }

    if ((sw=switch_find(switches, "--relative")) && sw->seen) {
        set_diff("rel", sw->lexeme);
    }

    if ((sw=switch_find(switches, "--epsilon")) && sw->seen) {
        set_diff("eps", sw->lexeme);
    }

    if ((sw=switch_find(switches, "--diff")) && sw->seen) {
        /* Convert command- and/or space-separated words into a list of
         * symbols. */
        s = t = safe_strdup(sw->lexeme);
        if (s) {
            for (argc=0; argc<NELMTS(argv) && (word=strtok(t, ", ")); argc++) {
                argv[argc] = obj_new(C_SYM, word);
                t = NULL;
            }
            val = V_make_list(argc, argv);
            for (i=0; i<argc; i++) obj_dest(argv[i]);
            free(s);
        } else {
            val = NIL;
        }
        sym = obj_new(C_SYM, "$diff");
        sym_vbind(sym, val);
        val=NIL;
        sym = obj_dest(sym);
    }

    if ((sw=switch_find(switches, "--obase")) && sw->seen) {
        sym_bi_set("obase", sw->lexeme, NULL, NULL);
    }
    
    if ((sw=switch_find(switches, "--debug")) && sw->seen) {
        DBDebugAPI = 2;
    }

    if ((sw=switch_find(switches, "--lowlevel")) && sw->seen && pass==0) {
        sym_bi_set("lowlevel", sw->lexeme, NULL, NULL);
    }
    
    if ((sw=switch_find(switches, "--rdonly")) && sw->seen) {
        sprintf(tmp, "%d", sw->value.d); /*boolean*/
        sym_bi_set("rdonly", tmp, NULL, NULL);
    }

    if ((sw=switch_find(switches, "--single")) && sw->seen) {
        DBForceSingle(1);
    }
    
    if ((sw=switch_find(switches, "--height")) && sw->seen) {
        sym_bi_set("height", sw->lexeme, NULL, NULL);
        OUT_NROWS = sw->value.d;
    }
    
    if ((sw=switch_find(switches, "--width")) && sw->seen) {
        sym_bi_set("width", sw->lexeme, NULL, NULL);
        OUT_NCOLS = MAX(1, sw->value.d);
    }

    if ((sw=switch_find(switches, "--verbose")) && sw->seen) {
        sym_bi_set("verbosity", "2", NULL, NULL);
        Verbosity = 2;
    } else if ((sw=switch_find(switches, "--quiet")) && sw->seen) {
        sym_bi_set("verbosity", "0", NULL, NULL);
        Verbosity = 0;
    }

    if ((sw=switch_find(switches, "--checksums")) && sw->seen) {
        sym_bi_set("checksums", "1", NULL, NULL);
    }
    
}

/*---------------------------------------------------------------------------
 * Purpose:     Print an error message about an unrecognized command-line
 *              switch.
 *
 * Programmer:  Robb Matzke
 *              Thursday, June  1, 2000
 *
 * Modifications:
 *---------------------------------------------------------------------------
 */
static void
bad_switch(const char *fmt, ...)
{
    va_list     ap;
    
    /* Base name of executable */
    const char  *base = strrchr(arg0, '/');
    base = base ? base+1 : arg0;

    /* Print the error message */
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", base);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    fprintf(stderr, "Say `%s --help' for usage.\n", base);
    va_end(ap);
}

/*-------------------------------------------------------------------------
 * Function:    main
 *
 * Purpose:     A better SILO browser.
 *
 * Return:      Success:        exit(0)
 *
 *              Failure:        exit(non-zero)
 *
 * Programmer:  Robb Matzke
 *              matzke@viper.llnl.gov
 *              Dec  4 1996
 *
 * Modifications:
 *
 *      Robb Matzke, 10 Feb 1997
 *      If the GNU readline and/or history libraries are available then
 *      initialize them.
 *
 *      Robb Matzke, 25 Feb 1997
 *      Added the `-A N' and `-R N' command-line options for setting the
 *      absolute and relative differencing tolerances.
 *
 *      Sean Ahern, Fri Feb 28 14:14:24 PST 1997
 *      Added checks for the readline library.
 *
 *      Robb Matzke, 5 Mar 1997
 *      Changed INIT_DIRECTORY to PUBLIC_INIT_FILE.  The base name of
 *      that file does not have to be the same as the base name of the
 *      user's personal init file in his/her home directory.
 *
 *      Eric Brugger, Fri Mar 14 15:42:55 PST 1997
 *      I modified the routine to register the ale3d and debug filters.
 *
 *      Robb Matzke, 29 Jul 1997
 *      We check for and print footnotes after each interactive command.
 *
 *      Robb Matzke, 29 Jul 1997
 *      If the `trap_fpe' variable is defined and non-zero then we ignore
 *      floating point exceptions.  Some machines raise SIGFPE when
 *      operating on NaN quantities and ignoring SIGFPE will convert them
 *      to zeros, which isn't necessarily desired either.  The `trap_fpe'
 *      variable is set with the `-nofpe' command-line switch and cleared
 *      with `-fpe'
 *
 *      Robb Matzke, 2 Sep 1997
 *      Added the `--debug' option that prints the names of each silo
 *      API function to stdout as it's called.  This is not for normal users
 *      and is therefore not documented in the user manual.
 *
 *      Robb Matzke, 2 Apr 1999
 *      Added the `-s' option which causes the browser to call
 *      DBForceSingle() before doing much of anything.
 *
 *      Robb Matzke, 2000-05-23
 *      The `-l' switch can take an integer argument, which is the value
 *      which is assigned to the `$lowlevel' internal variable. The
 *      default is one.
 *
 *      Robb Matzke, 2000-05-31
 *      Added long command-line switches.
 *
 *      Robb Matzke, 2000-06-02
 *      Calls DBShowErrors(DB_NONE) to turn off silo's error messages.
 *
 *      Robb Matzke, 2000-06-02
 *      Changed SIGFPE behavior to make it automatic and quieter.
 *
 *      Robb Matzke, 2000-06-29
 *      Added the `-E' and `--exclude' command-line switches.
 *
 *      Robb Matzke, 2000-07-10
 *      Added support for the `include' command by passing an input stack to
 *      the lexical analysis functions instead of just a single input
 *      source.
 *
 *      Robb Matzke, 2000-10-19
 *      Added `--obase' command-line switch.
 *
 *      Mark C. Miller, Tue Jul 22 17:49:15 PDT 2008
 *      Added call to setlinebuf on stdout. This is to prevent situations
 *      where a client shell using i/o-redirection of stderr into stdout
 *      (e.g. '2>&1') results in browser output lines getting 'interrupted'
 *      with lines from stderr.
 *
 *      Mark C. Miller, Wed Sep  2 16:47:43 PDT 2009
 *      Added an argument to process_switches to indicate which pass
 *      is being made.
 *
 *      Mark C. Miller, Wed Nov 11 22:18:17 PST 2009
 *      Added suppot for alternate relative diff option epsilon switch.
 *      Improved help message for various diff options.
 *
 *      Mark C. Miller, Tue Nov 17 22:32:01 PST 2009
 *      Fixed help messages so difference equations would not get split
 *      across new lines.
 *
 *      Mark C. Miller, Fri Feb 12 08:41:04 PST 2010
 *      Added the --split-vfd switch.
 *-------------------------------------------------------------------------
 */
int
main(int argc, char *argv[])
{
    obj_t        in=NIL, out=NIL;
    lex_t        *input_stack=NULL, *tmp_file=NULL;
    int          i, argno;
    char         init_file_buf[1024];
    const char   *init_file=NULL;
    struct passwd *passwd=NULL;
    strlist_t    eval_list, exclude_list, split_vfd_exts;
    switches_t   *sws=switch_new();
    switch_t     *sw=NULL;
    struct sigaction action;
    
    arg0 = argv[0]; /*global executable name*/

    /* We want stdout line buffered to prevent possible redirection
       from client shells (e.g. 2>&1) from having stderr cause
       breaks in lines in stdout */
    setlinebuf(stdout);

#if defined(HAVE_READLINE_READLINE_H) && defined(HAVE_LIBREADLINE)
    /* We have our own readline completion function that tries to complete
     * object names instead of the standard completion function that tries
     * to complete file names. */
    rl_completion_entry_function = (char *(*)(const char *, int)) browser_rl_obj_generator;
#endif /*HAVE_READLINE_READLINE_H && HAVE_LIBREADLINE*/

#if defined(HAVE_READLINE_HISTORY_H) && defined(HAVE_READLINE_HISTORY)
    /* History functions will be used.  A command history will be saved
     * in the HISTORY_FILE. */
    using_history();
    stifle_history(HISTORY_STIFLE);
#ifdef HISTORY_FILE
    if ((passwd=getpwuid(getuid())) && passwd->pw_dir) {
        assert (strlen(passwd->pw_dir)+strlen(HISTORY_FILE)+2 <
                sizeof(HistoryFile));
        sprintf (HistoryFile, "%s/%s", passwd->pw_dir, HISTORY_FILE);
        read_history (HistoryFile);
    } else {
        HistoryFile[0] = '\0';
    }
#endif /*HISTORY_FILE*/
#endif /*HAVE_READLINE_HISTORY_H && HAVE_READLINE_HISTORY*/

    /* Turn off Silo error reporting */
    DBShowErrors(DB_TOP, NULL);

    /* Initialize data structures. */
    out_init();
    obj_init();
    parse_init();
    sym_init();
    stc_silo_types();

    /* Determine where the default startup file is located. Read either
     * the user's init file or the system-wide init file, but not both.
     * The `--file' command-line option overrides this. */
    if ((passwd=getpwuid(getuid())) && passwd->pw_dir) {
        sprintf(init_file_buf, "%s/%s", passwd->pw_dir, INIT_FILE);
        if (access(init_file_buf, F_OK)>=0) init_file = init_file_buf;
    }
#ifdef PUBLIC_INIT_FILE
    if (!init_file) {
        strcpy(init_file_buf, PUBLIC_INIT_FILE);
        if (access(init_file_buf, F_OK)>=0) init_file = init_file_buf;
    }
#endif /*PUBLIC_INIT_FILE*/

    /* Define command-line switches */
    switch_add(sws, "-A", "--absolute", "g:ATOL",        NULL);
    switch_doc(NULL,
               "All absolute differencing tolerances are set to ATOL. "
               "Two numbers, A and B, are different if\n|A-B|>ATOL. "
               "This sets the internal variables `$diff_*_abs' where `*' "
               "is one of the following words: int8, short, int, long, "
               "float, or double. Default is zero.");
   
    switch_add(sws, "-R", "--relative", "g:RTOL",        NULL);
    switch_doc(NULL,
               "All relative differencing tolerances are set to RTOL. "
               "Two numbers, A and B, are different if\n2|A-B|/|A+B|>RTOL. "
               "This sets the internal variables `$diff_*_rel' where `*' "
               "is one of the following words: int8, short, int, long "
               "float, or double. Default is zero.");
   
    switch_add(sws, "-x",   "--epsilon", "g:EPS",        NULL);
    switch_doc(NULL,
               "When non-negative, all relative differencing epsilon "
               "parameters are set to EPS "
               "and an alternate relative difference scheme is used where "
               "two numbers, A and B, are different if\n|A-B|/(|A|+|B|+EPS)>RTOL. "
               "This sets the internal variables `$diff_*_eps' where `*' "
               "is one of the following words: int8, short, int, long "
               "float, or double. For EPS=0, the algorithm is similar (but not "
               "identical) to `normal' relative differencing. But for EPS=1, "
               "it behaves in such a way as to shift between this alternate "
               "relative differencing for large numbers and absolute differencing "
               "for numbers near zero. Default is -1 (e.g. turned off).");
   
    switch_add(sws, "-V", "--version",  NULL,           process_sw_version);
    switch_doc(NULL, "Shows the version number.");
   
    switch_add(sws, "-d", "--diff",     "s:WORDS",      NULL);
    switch_doc(NULL,
               "Controls the details of the `diff' function. WORDS is a "
               "comma-separated list of key words: detail, brief, summary, "
               "ignore_additions, ignore_deletions. See documentation for "
               "the `$diff' variable for details.");

    switch_add(sws, "-e", "--eval",     "s:EXPR",       process_sw_eval);
    memset(&eval_list, 0, sizeof eval_list);
    switch_info(NULL, &eval_list);
    switch_doc(NULL,
               "The browser expression EXPR is evaluated and printed on the "
               "standard output stream. More than one expression can be given "
               "to the browser by specifying this switch multiple times. The "
               "expressions are evaluated in the order given.  When this "
               "switch is specified the browser does not enter interactive "
               "mode.");

    switch_add(sws, "-E", "--exclude",  "s:NAMES",      process_sw_exclude);
    memset(&exclude_list, 0, sizeof exclude_list);
    switch_info(NULL, &exclude_list);
    switch_doc(NULL,
               "The browser $exclude variable is set to the comma- and/or "
               "space-separated list of names supplied as the switch's "
               "argument. This switch may appear more than once. The names "
               "in the $exclude list will be excluded from recursive `diff' "
               "operations.");
    
    switch_add(sws, "-f", "--file",     "s:FILE",       NULL);
    switch_doc(NULL,
               "When the browser begins execution it attempts to read the "
               "file `.browser_rc' in the user's home directory. If that "
               "file isn't readable then it tries to read a system-wide "
               "browser initialization file. This command line switch causes "
               "the specified file to be read instead. The file is read after "
               "command-line options are parsed and command-line specified "
               "database files are opened. After processing the file the "
               "command-line options will be re-evaluated to override "
               "settings in the startup file. To prevent the browser from "
               "reading any initialization file, give the name "
               "`/dev/null'. Starting the browser with `--verbose' will "
               "cause it to display the name of the startup file it's using.");
   
    switch_add(sws, "-h", "--help",     NULL,           process_sw_help);
    switch_doc(NULL,
               "Show a usage summary. The browser `help' command can "
               "provide more detailed documentation.");
   
    switch_add(sws, "-l", "--lowlevel", "u:N=1",        NULL);
    switch_doc(NULL,
               "This option sets the internal variable `$lowlevel' to N "
               "(default 1), which causes the browser to load database "
               "objects as type DBobject even if the object is some other "
               "composite SILO datatype like DBquadvar.\n");

    switch_add(sws, "-n", "--height",    "u:LINES",     NULL);
    switch_doc(NULL,
               "The browser pages interactive output to prevent it from "
               "scrolling off the top of the screen. It normally decides "
               "how tall the window is by making ioctl() calls, but this "
               "switch can be used to override that choice. It sets the "
               "$height variable to LINES.");
   
    switch_add(sws, "-q", "--quiet",    NULL,           process_sw_verbose);
    switch_doc(NULL, "Cause the browser to be less verbose than normal by "
               "setting the $verbosity variable to zero (default is one).");


    switch_add(sws, "-o", "--obase",   "u:FORMAT",     NULL);
    switch_doc(NULL,
               "This switch sets the internal variable `$obase' to the "
               "FORMAT base, which should be one of 16, 8, or 2 for "
               "hexadecimal, octal, or binary output format. It controls how "
               "integer, floating-point, character, and string data are "
               "displayed. The default is that these types of data are "
               "displayed with a type-dependent printf(3C) format string "
               "which is user defined (e.g., `$fmt_int').");

    switch_add(sws, "-r", "--rdonly",   "b=1",          NULL);
    switch_doc(NULL,
               "If this switch is specified then all files will be open in "
               "read-only mode regardless of their permissions. This switch "
               "sets the $rdonly variable.");

    switch_add(sws, "-s", "--single",   "b=1",          NULL);
    switch_doc(NULL,
               "Causes DBForceSingle() to be called with an argument of one, "
               "resulting in data being returned as type `float' instead of "
               "`double'.");

    switch_add(sws, "-v", "--verbose",  NULL,           process_sw_verbose);
    switch_doc(NULL, "Cause the browser to be more verbose than normal by "
               "setting the $verbosity variable to two (default is one).");

    switch_add(sws, "-w", "--width",    "u:COLUMNS",    NULL);
    switch_doc(NULL,
               "The browser normally breaks lines to prevent them from "
               "wrapping around from the right margin to the next line. It "
               "decides how many columns are appropriate by making ioctl() "
               "calls, but this switch overrides that choice. This switch "
               "sets $width to COLUMNS.");

    switch_add(sws, NULL, "--debug",    "b=1",          NULL);
    switch_doc(NULL, "Tells SILO to display the names of the SILO API "
               "functions as they are called. This switch is probably only "
               "useful to those who debug the browser.");

    switch_add(sws, "-c", "--checksums", NULL,        NULL);
    switch_doc(NULL,
               "This option sets the internal variable `$checksums' to 1 "
               "(default 0), which causes the browser to perform checksums, "
               "when available in the database, during read.\n");

    switch_add(sws, NULL, "--split-vfd", "s:EXTENSIONS", process_sw_split_vfd);
    memset(&split_vfd_exts, 0, sizeof split_vfd_exts);
    switch_info(NULL, &split_vfd_exts);
    switch_doc(NULL,
               "Tells browser to use the split virtual file driver (vfd) to "
               "open the file and, optionally, the extensions used to identify "
               "the different file parts.\n");

    /* Parse, then process command-line options */
    Switches = sws;
    if ((argno=switch_parse(sws, argc, argv, bad_switch))<0) exit(1);
    process_switches(sws, 0);

    /* Assign the --exclude list to the $exclude variable */
    if ((sw=switch_find(sws, "--exclude")) && sw->seen) {
        obj_t list[NELMTS(exclude_list.value)], symbol, value;
        for (i=0; i<exclude_list.nused; i++) {
            list[i] = obj_new(C_STR, exclude_list.value[i]);
        }
        value = V_make_list(exclude_list.nused, list);
        for (i=0; i<exclude_list.nused; i++) {
            obj_dest(list[i]);
        }
        symbol = obj_new(C_SYM, "$exclude");
        sym_vbind(symbol, value);
        obj_dest(symbol);
    }
    
    /* Assign the --split-vfd values list to the $split_vfd_exts variable */
    if ((sw=switch_find(sws, "--split-vfd")) && sw->seen) {
        obj_t list[NELMTS(split_vfd_exts.value)], symbol, value;
        for (i=0; i<split_vfd_exts.nused; i++) {
            list[i] = obj_new(C_STR, split_vfd_exts.value[i]);
        }
        value = V_make_list(split_vfd_exts.nused, list);
        for (i=0; i<split_vfd_exts.nused; i++) {
            obj_dest(list[i]);
        }
        symbol = obj_new(C_SYM, "$splitvfdexts");
        sym_vbind(symbol, value);
        obj_dest(symbol);
    }
    
    /* If invoked with --help then exit now.  If invoked with just
    * `--version' and no other arguments then exit now. */
    if (((sw=switch_find(sws, "--help")) && sw->seen) ||
        ((sw=switch_find(sws, "--version")) && sw->seen && 2==argc)) {
        exit(0);
    }
   
    /* Register the ale3d and debug filters. */    
    DBFilterRegistration("ale3d", NULL, f_ale3d_Open);
    DBFilterRegistration("debug", NULL, f_debug_Open);

    /* Remaining words on the command-line are file names.  Open them as
     * `$1', `$2', etc. */
    for (i=1; argno<argc; argno++,i++) {
        char sym[16];
        obj_t av[2];

        sprintf(sym, "$%d", i);
        av[0] = obj_new(C_SYM, argv[argno]);
        av[1] = obj_new(C_SYM, sym);
        out = V_open(2, av);
        obj_dest(av[0]);
        obj_dest(av[1]);
        out = obj_dest(out);
    }

    /* Don't dump core on floating-point exceptions. Keep track of window
     * size changes. */
    action.sa_handler = sigwinch_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &action, NULL);

    action.sa_handler = sigfpe_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGFPE, &action, NULL);

    /* Open the initialization file and read it. */    
    if ((sw=switch_find(sws, "--file")) && sw->seen) init_file = sw->lexeme;
    if (init_file) {
        input_stack = lex_stack();
        if (NULL==(tmp_file=lex_open(init_file))) {
            out_errorn("cannot open initialization file `%s'", init_file);
            exit(1);
        } else {
            if (Verbosity>=2) {
                out_info("reading commands from `%s'", init_file);
            }
            lex_push(input_stack, tmp_file);
            for (;;) {
                in = parse_stmt(input_stack, true);
                if (in && C_SYM==in->pub.cls &&
                    !strcmp(obj_name(in), "__END__")) {
                    break;
                }
                out = obj_eval(in);
                in = obj_dest(in);
                out = obj_dest(out);
                rep_update();
            }
        }
        input_stack = lex_close(input_stack);
        check_version(init_file);
    } else if (Verbosity>=2) {
        out_info("no initialization file -- using built-in defaults");
    }
    

    /* Flags set on the command-line override values set in the initilization
    * file, so reprocess the command-line switches. */
    process_switches(sws, 1);

    /* Process expressions given on the command line.  A control-C should
     * terminate the command and the browser. If there were expressions
    * then don't enter interactive mode. */
    for (i=0; i<eval_list.nused; i++) {
        input_stack = lex_stack();
        lex_push(input_stack, lex_string(eval_list.value[i]));
        for (;;) {
            in = parse_stmt(input_stack, true);
            if (in && C_SYM==in->pub.cls && !strcmp(obj_name(in), "__END__")) {
                break;
            }
            out_reset(OUT_STDOUT);
            out = obj_eval(in);
            if (PAGER_INTERRUPT==out_brokenpipe(OUT_STDOUT)) {
                fflush(OUT_STDOUT->f);
                fputs("\nCaught SIGINT.\n", stdout);
                exit(1);
            }
            if (out || Verbosity>=2) {
                obj_print(out, OUT_STDOUT);
                out_nl(OUT_STDOUT);
            }
            in = obj_dest(in);
            out = obj_dest(out);
            rep_update();
        }
        input_stack = lex_close(input_stack);
    }
    if (eval_list.nused) exit(0);

#if !defined(HAVE_READLINE_READLINE_H) || !defined(HAVE_LIBREADLINE)
    out_info("Command-line editing is disabled (no readline library).");
#endif

    /* Now process interactive input. */
    input_stack = lex_stack();
    lex_push(input_stack, lex_stream(stdin));

    ary_footnotes_reset();
    for (;;) {
        in = parse_stmt(input_stack, true);
        if (in && C_SYM==in->pub.cls && !strcmp(obj_name(in), "__END__")) {
            break;
        }

        out_reset(OUT_STDOUT);
        rep_update(); /*handle sigs that arrived during input */
        out = obj_eval(in);
        if (PAGER_INTERRUPT==out_brokenpipe(OUT_STDOUT)) {
            fflush(OUT_STDOUT->f);
            fputs("\nCommand aborted.\n", stdout);
            in = obj_dest(in);
            out = obj_dest(out);
            continue;
        }

        if (out || Verbosity>=2) {
            obj_print(out, OUT_STDOUT);
            out_nl(OUT_STDOUT);
        }

        ary_footnotes_print();
        in = obj_dest(in);
        out = obj_dest(out);
        rep_update(); /*handle changes to state from eval */
        if (Verbosity>=2) out_info("Objects allocated: %d", obj_usage());
    }

    lex_close(input_stack);
    return 0;
}
