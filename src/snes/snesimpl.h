
#ifndef __SNESIMPL_H
#define __SNESIMPL_H

#include "petscsnes.h"

/*
   Nonlinear solver context
 */
#define MAXSNESMONITORS 5

struct _p_SNES {
  PETSCHEADER(int);

  /* Identifies this as a grid SNES structure */
  PetscTruth  isGSNES;                          /* This problem arises from an underlying grid */

  /*  ------------------------ User-provided stuff -------------------------------*/
  void  *user;		                        /* user-defined context */

  Vec   vec_sol,vec_sol_always;                 /* pointer to solution */
  Vec   vec_sol_update_always;                  /* pointer to solution update */

  PetscErrorCode (*computefunction)(SNES,Vec,Vec,void*); /* function routine */
  Vec            vec_func,vec_func_always;               /* pointer to function */
  Vec            afine;                                  /* If non-null solve F(x) = afine */
  void           *funP;                                  /* user-defined function context */

  PetscErrorCode (*computejacobian)(SNES,Vec,Mat*,Mat*,MatStructure*,void*);
  Mat            jacobian;                               /* Jacobian matrix */
  Mat            jacobian_pre;                           /* preconditioner matrix */
  void           *jacP;                                  /* user-defined Jacobian context */
  KSP            ksp;                                   /* linear solver context */

  PetscErrorCode (*computescaling)(Vec,Vec,void*);       /* scaling routine */
  Vec            scaling;                                /* scaling vector */
  void           *scaP;                                  /* scaling context */

  /* ------------------------Boundary conditions-----------------------------------*/
  PetscErrorCode (*applyrhsbc)(SNES, Vec, void *);         /* Applies boundary conditions to the rhs */
  PetscErrorCode (*applysolbc)(SNES, Vec, void *);         /* Applies boundary conditions to the solution */

  /* ------------------------Time stepping hooks-----------------------------------*/
  PetscErrorCode (*update)(SNES, PetscInt);                     /* General purpose function for update */

  /* ---------------- PETSc-provided (or user-provided) stuff ---------------------*/

  PetscErrorCode (*monitor[MAXSNESMONITORS])(SNES,PetscInt,PetscReal,void*); /* monitor routine */
  PetscErrorCode (*monitordestroy[MAXSNESMONITORS])(void*);          /* monitor context destroy routine */
  void           *monitorcontext[MAXSNESMONITORS];                   /* monitor context */
  PetscInt       numbermonitors;                                     /* number of monitors */
  PetscErrorCode (*converged)(SNES,PetscReal,PetscReal,PetscReal,SNESConvergedReason*,void*);      /* convergence routine */
  void           *cnvP;	                                            /* convergence context */
  SNESConvergedReason reason;

  /* --- Routines and data that are unique to each particular solver --- */

  PetscErrorCode (*setup)(SNES);             /* routine to set up the nonlinear solver */
  PetscInt       setupcalled;                /* true if setup has been called */
  PetscErrorCode (*solve)(SNES);             /* actual nonlinear solver */
  PetscErrorCode (*setfromoptions)(SNES);    /* sets options from database */
  PetscErrorCode (*printhelp)(SNES,char*);   /* prints help info */
  void           *data;                      /* implementation-specific data */

  /* --------------------------  Parameters -------------------------------------- */

  PetscInt    max_its;            /* max number of iterations */
  PetscInt    max_funcs;          /* max number of function evals */
  PetscInt    nfuncs;             /* number of function evaluations */
  PetscInt    iter;               /* global iteration number */
  PetscInt    linear_its;         /* total number of linear solver iterations */
  PetscReal   norm;            /* residual norm of current iterate */
  PetscReal   rtol;            /* relative tolerance */
  PetscReal   abstol;            /* absolute tolerance */
  PetscReal   xtol;            /* relative tolerance in solution */
  PetscReal   deltatol;        /* trust region convergence tolerance */
  PetscTruth  printreason;     /* print reason for convergence/divergence after each solve */
  /* ------------------------ Default work-area management ---------------------- */

  PetscInt    nwork;              
  Vec         *work;

  /* ------------------------- Miscellaneous Information ------------------------ */

  PetscReal   *conv_hist;         /* If !0, stores function norm (or
                                    gradient norm) at each iteration */
  PetscInt    *conv_hist_its;     /* linear iterations for each Newton step */
  PetscInt    conv_hist_len;      /* size of convergence history array */
  PetscInt    conv_hist_max;      /* actual amount of data in conv_history */
  PetscTruth  conv_hist_reset;    /* reset counter for each new SNES solve */
  PetscInt    numFailures;        /* number of unsuccessful step attempts */
  PetscInt    maxFailures;        /* maximum number of unsuccessful step attempts */

 /*
   These are REALLY ugly and don't belong here, but since they must 
  be destroyed at the conclusion we have to put them somewhere.
 */
  PetscTruth  ksp_ewconv;        /* flag indicating use of Eisenstat-Walker KSP convergence criteria */
  void        *kspconvctx;       /* KSP convergence context */

  PetscReal   ttol;           /* used by default convergence test routine */

  Vec             *vwork;            /* more work vectors for Jacobian approx */
  PetscInt        nvwork;
  PetscErrorCode (*destroy)(SNES);
  PetscErrorCode (*view)(SNES,PetscViewer);
};

/* Context for Eisenstat-Walker convergence criteria for KSP solvers */
typedef struct {
  PetscInt  version;             /* flag indicating version 1 or 2 of test */
  PetscReal rtol_0;              /* initial rtol */
  PetscReal rtol_last;           /* last rtol */
  PetscReal rtol_max;            /* maximum rtol */
  PetscReal gamma;               /* mult. factor for version 2 rtol computation */
  PetscReal alpha;               /* power for version 2 rtol computation */
  PetscReal alpha2;              /* power for safeguard */
  PetscReal threshold;           /* threshold for imposing safeguard */
  PetscReal lresid_last;         /* linear residual from last iteration */
  PetscReal norm_last;           /* function norm from last iteration */
} SNES_KSP_EW_ConvCtx;

#define SNESLogConvHistory(snes,res,its) \
  { if (snes->conv_hist && snes->conv_hist_max > snes->conv_hist_len) \
    { if (snes->conv_hist)     snes->conv_hist[snes->conv_hist_len]     = res; \
      if (snes->conv_hist_its) snes->conv_hist_its[snes->conv_hist_len] = its; \
      snes->conv_hist_len++;\
    }}

#define SNESMonitor(snes,it,rnorm) \
        { PetscErrorCode _ierr; PetscInt _i,_im = snes->numbermonitors; \
          for (_i=0; _i<_im; _i++) {\
            _ierr = (*snes->monitor[_i])(snes,it,rnorm,snes->monitorcontext[_i]);CHKERRQ(_ierr); \
	  } \
	}

PetscErrorCode SNES_KSP_EW_Converged_Private(KSP,PetscInt,PetscReal,KSPConvergedReason*,void*);
PetscErrorCode SNES_KSP_EW_ComputeRelativeTolerance_Private(SNES,KSP);
PetscErrorCode SNESScaleStep_Private(SNES,Vec,PetscReal*,PetscReal*,PetscReal*,PetscReal*);

#endif
