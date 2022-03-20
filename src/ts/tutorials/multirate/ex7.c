static const char help[] = "1D periodic Finite Volume solver by a particular slope limiter with semidiscrete time stepping.\n"
  "  advection   - Constant coefficient scalar advection\n"
  "                u_t       + (a*u)_x               = 0\n"
  "  for this toy problem, we choose different meshsizes for different sub-domains, say\n"
  "                hxs  = (xmax - xmin)/2.0*(hratio+1.0)/Mx, \n"
  "                hxf  = (xmax - xmin)/2.0*(1.0+1.0/hratio)/Mx, \n"
  "  with x belongs to (xmin,xmax), the number of total mesh points is Mx and the ratio between the meshsize of corse\n\n"
  "  grids and fine grids is hratio.\n"
  "  exact       - Exact Riemann solver which usually needs to perform a Newton iteration to connect\n"
  "                the states across shocks and rarefactions\n"
  "  simulation  - use reference solution which is generated by smaller time step size to be true solution,\n"
  "                also the reference solution should be generated by user and stored in a binary file.\n"
  "  characteristic - Limit the characteristic variables, this is usually preferred (default)\n"
  "Several initial conditions can be chosen with -initial N\n\n"
  "The problem size should be set with -da_grid_x M\n\n"
  "This script choose the slope limiter by biased second-order upwind procedure which is proposed by Van Leer in 1994\n"
  "                             u(x_(k+1/2),t) = u(x_k,t) + phi(x_(k+1/2),t)*(u(x_k,t)-u(x_(k-1),t))                 \n"
  "                     limiter phi(x_(k+1/2),t) = max(0,min(r(k+1/2),min(2,gamma(k+1/2)*r(k+1/2)+alpha(k+1/2))))    \n"
  "                             r(k+1/2) = (u(x_(k+1))-u(x_k))/(u(x_k)-u(x_(k-1)))                                   \n"
  "                             alpha(k+1/2) = (h_k*h_(k+1))/(h_(k-1)+h_k)/(h_(k-1)+h_k+h_(k+1))                     \n"
  "                             gamma(k+1/2) = h_k*(h_(k-1)+h_k)/(h_k+h_(k+1))/(h_(k-1)+h_k+h_(k+1))                 \n";

#include <petscts.h>
#include <petscdm.h>
#include <petscdmda.h>
#include <petscdraw.h>
#include <petscmath.h>

static inline PetscReal RangeMod(PetscReal a,PetscReal xmin,PetscReal xmax) { PetscReal range = xmax-xmin; return xmin +PetscFmodReal(range+PetscFmodReal(a,range),range); }

/* --------------------------------- Finite Volume data structures ----------------------------------- */

typedef enum {FVBC_PERIODIC, FVBC_OUTFLOW} FVBCType;
static const char *FVBCTypes[] = {"PERIODIC","OUTFLOW","FVBCType","FVBC_",0};

typedef struct {
  PetscErrorCode (*sample)(void*,PetscInt,FVBCType,PetscReal,PetscReal,PetscReal,PetscReal,PetscReal*);
  PetscErrorCode (*flux)(void*,const PetscScalar*,PetscScalar*,PetscReal*);
  PetscErrorCode (*destroy)(void*);
  void           *user;
  PetscInt       dof;
  char           *fieldname[16];
} PhysicsCtx;

typedef struct {
  PhysicsCtx  physics;
  MPI_Comm    comm;
  char        prefix[256];

  /* Local work arrays */
  PetscScalar *flux;            /* Flux across interface                                                      */
  PetscReal   *speeds;          /* Speeds of each wave                                                        */
  PetscScalar *u;               /* value at face                                                              */

  PetscReal   cfl_idt;          /* Max allowable value of 1/Delta t                                           */
  PetscReal   cfl;
  PetscReal   xmin,xmax;
  PetscInt    initial;
  PetscBool   exact;
  PetscBool   simulation;
  FVBCType    bctype;
  PetscInt    hratio;           /* hratio = hslow/hfast */
  IS          isf,iss;
  PetscInt    sf,fs;            /* slow-fast and fast-slow interfaces */
} FVCtx;

/* --------------------------------- Physics ----------------------------------- */
static PetscErrorCode PhysicsDestroy_SimpleFree(void *vctx)
{
  PetscErrorCode ierr;

  PetscFunctionBeginUser;
  ierr = PetscFree(vctx);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* --------------------------------- Advection ----------------------------------- */
typedef struct {
  PetscReal a;                  /* advective velocity */
} AdvectCtx;

static PetscErrorCode PhysicsFlux_Advect(void *vctx,const PetscScalar *u,PetscScalar *flux,PetscReal *maxspeed)
{
  AdvectCtx *ctx = (AdvectCtx*)vctx;
  PetscReal speed;

  PetscFunctionBeginUser;
  speed     = ctx->a;
  flux[0]   = speed*u[0];
  *maxspeed = speed;
  PetscFunctionReturn(0);
}

static PetscErrorCode PhysicsSample_Advect(void *vctx,PetscInt initial,FVBCType bctype,PetscReal xmin,PetscReal xmax,PetscReal t,PetscReal x,PetscReal *u)
{
  AdvectCtx *ctx = (AdvectCtx*)vctx;
  PetscReal a    = ctx->a,x0;

  PetscFunctionBeginUser;
  switch (bctype) {
    case FVBC_OUTFLOW:   x0 = x-a*t; break;
    case FVBC_PERIODIC: x0 = RangeMod(x-a*t,xmin,xmax); break;
    default: SETERRQ(PETSC_COMM_SELF,PETSC_ERR_ARG_UNKNOWN_TYPE,"unknown BCType");
  }
  switch (initial) {
    case 0: u[0] = (x0 < 0) ? 1 : -1; break;
    case 1: u[0] = (x0 < 0) ? -1 : 1; break;
    case 2: u[0] = (0 < x0 && x0 < 1) ? 1 : 0; break;
    case 3: u[0] = PetscSinReal(2*PETSC_PI*x0); break;
    case 4: u[0] = PetscAbs(x0); break;
    case 5: u[0] = (x0 < 0 || x0 > 0.5) ? 0 : PetscSqr(PetscSinReal(2*PETSC_PI*x0)); break;
    case 6: u[0] = (x0 < 0) ? 0 : ((x0 < 1) ? x0 : ((x0 < 2) ? 2-x0 : 0)); break;
    case 7: u[0] = PetscPowReal(PetscSinReal(PETSC_PI*x0),10.0);break;
    default: SETERRQ(PETSC_COMM_SELF,PETSC_ERR_ARG_UNKNOWN_TYPE,"unknown initial condition");
  }
  PetscFunctionReturn(0);
}

static PetscErrorCode PhysicsCreate_Advect(FVCtx *ctx)
{
  PetscErrorCode ierr;
  AdvectCtx      *user;

  PetscFunctionBeginUser;
  ierr = PetscNew(&user);CHKERRQ(ierr);
  ctx->physics.sample         = PhysicsSample_Advect;
  ctx->physics.flux           = PhysicsFlux_Advect;
  ctx->physics.destroy        = PhysicsDestroy_SimpleFree;
  ctx->physics.user           = user;
  ctx->physics.dof            = 1;
  ierr = PetscStrallocpy("u",&ctx->physics.fieldname[0]);CHKERRQ(ierr);
  user->a = 1;
  ierr = PetscOptionsBegin(ctx->comm,ctx->prefix,"Options for advection","");CHKERRQ(ierr);
  {
    ierr = PetscOptionsReal("-physics_advect_a","Speed","",user->a,&user->a,NULL);CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd();CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* --------------------------------- Finite Volume Solver ----------------------------------- */

static PetscErrorCode FVRHSFunction(TS ts,PetscReal time,Vec X,Vec F,void *vctx)
{
  FVCtx          *ctx = (FVCtx*)vctx;
  PetscErrorCode ierr;
  PetscInt       i,j,Mx,dof,xs,xm,sf = ctx->sf,fs = ctx->fs;
  PetscReal      hf,hs,cfl_idt = 0;
  PetscScalar    *x,*f,*r,*min,*alpha,*gamma;
  Vec            Xloc;
  DM             da;

  PetscFunctionBeginUser;
  ierr = TSGetDM(ts,&da);CHKERRQ(ierr);
  ierr = DMGetLocalVector(da,&Xloc);CHKERRQ(ierr);                          /* Xloc contains ghost points                                     */
  ierr = DMDAGetInfo(da,0,&Mx,0,0,0,0,0,&dof,0,0,0,0,0);CHKERRQ(ierr);   /* Mx is the number of center points                              */
  hs   = (ctx->xmax-ctx->xmin)/2.0*(ctx->hratio+1.0)/Mx;
  hf   = (ctx->xmax-ctx->xmin)/2.0*(1.0+1.0/ctx->hratio)/Mx;
  ierr = DMGlobalToLocalBegin(da,X,INSERT_VALUES,Xloc);CHKERRQ(ierr);       /* X is solution vector which does not contain ghost points       */
  ierr = DMGlobalToLocalEnd(da,X,INSERT_VALUES,Xloc);CHKERRQ(ierr);
  ierr = VecZeroEntries(F);CHKERRQ(ierr);                                   /* F is the right hand side function corresponds to center points */
  ierr = DMDAVecGetArray(da,Xloc,&x);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(da,F,&f);CHKERRQ(ierr);
  ierr = DMDAGetCorners(da,&xs,0,0,&xm,0,0);CHKERRQ(ierr);
  ierr = PetscMalloc4(dof,&r,dof,&min,dof,&alpha,dof,&gamma);CHKERRQ(ierr);

  if (ctx->bctype == FVBC_OUTFLOW) {
    for (i=xs-2; i<0; i++) {
      for (j=0; j<dof; j++) x[i*dof+j] = x[j];
    }
    for (i=Mx; i<xs+xm+2; i++) {
      for (j=0; j<dof; j++) x[i*dof+j] = x[(xs+xm-1)*dof+j];
    }
  }

  for (i=xs; i<xs+xm+1; i++) {
    PetscReal   maxspeed;
    PetscScalar *u;
    if (i < sf || i > fs+1) {
      u = &ctx->u[0];
      alpha[0] = 1.0/6.0;
      gamma[0] = 1.0/3.0;
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j]-x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr =  (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      cfl_idt = PetscMax(cfl_idt,PetscAbsScalar(maxspeed/hs));
      if (i > xs) {
        for (j=0; j<dof; j++) f[(i-1)*dof+j] -= ctx->flux[j]/hs;
      }
      if (i < xs+xm) {
        for (j=0; j<dof; j++) f[i*dof+j] += ctx->flux[j]/hs;
      }
    } else if (i == sf) {
      u = &ctx->u[0];
      alpha[0] = hs*hf/(hs+hs)/(hs+hs+hf);
      gamma[0] = hs*(hs+hs)/(hs+hf)/(hs+hs+hf);
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j]-x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr = (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      if (i > xs) {
        for (j=0; j<dof; j++) f[(i-1)*dof+j] -= ctx->flux[j]/hs;
      }
      if (i < xs+xm) {
        for (j=0; j<dof; j++) f[i*dof+j] += ctx->flux[j]/hf;
      }
    } else if (i == sf+1) {
      u = &ctx->u[0];
      alpha[0] = hf*hf/(hs+hf)/(hs+hf+hf);
      gamma[0] = hf*(hs+hf)/(hf+hf)/(hs+hf+hf);
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j] - x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr =  (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      if (i > xs) {
        for (j=0; j<dof; j++) f[(i-1)*dof+j] -= ctx->flux[j]/hf;
      }
      if (i < xs+xm) {
        for (j=0; j<dof; j++) f[i*dof+j] += ctx->flux[j]/hf;
      }
    } else if (i > sf+1 && i < fs) {
      u = &ctx->u[0];
      alpha[0] = 1.0/6.0;
      gamma[0] = 1.0/3.0;
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j] - x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr = (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      if (i > xs) {
        for (j=0; j<dof; j++) f[(i-1)*dof+j] -= ctx->flux[j]/hf;
      }
      if (i < xs+xm) {
        for (j=0; j<dof; j++) f[i*dof+j] += ctx->flux[j]/hf;
      }
    } else if (i == fs) {
      u = &ctx->u[0];
      alpha[0] = hf*hs/(hf+hf)/(hf+hf+hs);
      gamma[0] = hf*(hf+hf)/(hf+hs)/(hf+hf+hs);
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j] - x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr = (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      if (i > xs) {
        for (j=0; j<dof; j++) f[(i-1)*dof+j] -= ctx->flux[j]/hf;
      }
      if (i < xs+xm) {
        for (j=0; j<dof; j++) f[i*dof+j] += ctx->flux[j]/hs;
      }
    } else if (i == fs+1) {
      u = &ctx->u[0];
      alpha[0] = hs*hs/(hf+hs)/(hf+hs+hs);
      gamma[0] = hs*(hf+hs)/(hs+hs)/(hf+hs+hs);
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j] - x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr = (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      if (i > xs) {
        for (j=0; j<dof; j++) f[(i-1)*dof+j] -= ctx->flux[j]/hs;
      }
      if (i < xs+xm) {
        for (j=0; j<dof; j++) f[i*dof+j] += ctx->flux[j]/hs;
      }
    }
  }
  ierr = DMDAVecRestoreArray(da,Xloc,&x);CHKERRQ(ierr);
  ierr = DMDAVecRestoreArray(da,F,&f);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(da,&Xloc);CHKERRQ(ierr);
  ierr = MPI_Allreduce(&cfl_idt,&ctx->cfl_idt,1,MPIU_SCALAR,MPIU_MAX,PetscObjectComm((PetscObject)da));CHKERRMPI(ierr);
  if (0) {
    /* We need a way to inform the TS of a CFL constraint, this is a debugging fragment */
    PetscReal dt,tnow;
    ierr = TSGetTimeStep(ts,&dt);CHKERRQ(ierr);
    ierr = TSGetTime(ts,&tnow);CHKERRQ(ierr);
    if (dt > 0.5/ctx->cfl_idt) {
      ierr = PetscPrintf(ctx->comm,"Stability constraint exceeded at t=%g, dt %g > %g\n",(double)tnow,(double)dt,(double)(0.5/ctx->cfl_idt));CHKERRQ(ierr);
    }
  }
  ierr = PetscFree4(r,min,alpha,gamma);CHKERRQ(ierr);
  PetscFunctionReturn(0);
 }

static PetscErrorCode FVRHSFunctionslow(TS ts,PetscReal time,Vec X,Vec F,void *vctx)
{
  FVCtx             *ctx = (FVCtx*)vctx;
  PetscErrorCode    ierr;
  PetscInt          i,j,Mx,dof,xs,xm,islow = 0,sf = ctx->sf,fs = ctx->fs;
  PetscReal         hf,hs;
  PetscScalar       *x,*f,*r,*min,*alpha,*gamma;
  Vec               Xloc;
  DM                da;

  PetscFunctionBeginUser;
  ierr = TSGetDM(ts,&da);CHKERRQ(ierr);
  ierr = DMGetLocalVector(da,&Xloc);CHKERRQ(ierr);                          /* Xloc contains ghost points                                     */
  ierr = DMDAGetInfo(da,0,&Mx,0,0,0,0,0,&dof,0,0,0,0,0);CHKERRQ(ierr);   /* Mx is the number of center points                              */
  hs   = (ctx->xmax-ctx->xmin)/2.0*(ctx->hratio+1.0)/Mx;
  hf   = (ctx->xmax-ctx->xmin)/2.0*(1.0+1.0/ctx->hratio)/Mx;
  ierr = DMGlobalToLocalBegin(da,X,INSERT_VALUES,Xloc);CHKERRQ(ierr);       /* X is solution vector which does not contain ghost points       */
  ierr = DMGlobalToLocalEnd  (da,X,INSERT_VALUES,Xloc);CHKERRQ(ierr);
  ierr = VecZeroEntries(F);CHKERRQ(ierr);                                   /* F is the right hand side function corresponds to center points */
  ierr = DMDAVecGetArray(da,Xloc,&x);CHKERRQ(ierr);
  ierr = VecGetArray(F,&f);CHKERRQ(ierr);
  ierr = DMDAGetCorners(da,&xs,0,0,&xm,0,0);CHKERRQ(ierr);
  ierr = PetscMalloc4(dof,&r,dof,&min,dof,&alpha,dof,&gamma);CHKERRQ(ierr);

  if (ctx->bctype == FVBC_OUTFLOW) {
    for (i=xs-2; i<0; i++) {
      for (j=0; j<dof; j++) x[i*dof+j] = x[j];
    }
    for (i=Mx; i<xs+xm+2; i++) {
      for (j=0; j<dof; j++) x[i*dof+j] = x[(xs+xm-1)*dof+j];
    }
  }

  for (i=xs; i<xs+xm+1; i++) {
    PetscReal   maxspeed;
    PetscScalar *u;
    if (i < sf) {
      u = &ctx->u[0];
      alpha[0] = 1.0/6.0;
      gamma[0] = 1.0/3.0;
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j] - x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr = (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      if (i > xs) {
        for (j=0; j<dof; j++) f[(islow-1)*dof+j] -= ctx->flux[j]/hs;
      }
      if (i < xs+xm) {
        for (j=0; j<dof; j++) f[islow*dof+j] += ctx->flux[j]/hs;
        islow++;
      }
    } else if (i == sf) {
      u = &ctx->u[0];
      alpha[0] = hs*hf/(hs+hs)/(hs+hs+hf);
      gamma[0] = hs*(hs+hs)/(hs+hf)/(hs+hs+hf);
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j] - x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr = (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      if (i > xs) {
        for (j=0; j<dof; j++) f[(islow-1)*dof+j] -= ctx->flux[j]/hs;
      }
    } else if (i == fs) {
      u = &ctx->u[0];
      alpha[0] = hf*hs/(hf+hf)/(hf+hf+hs);
      gamma[0] = hf*(hf+hf)/(hf+hs)/(hf+hf+hs);
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j] - x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr = (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      if (i < xs+xm) {
        for (j=0; j<dof; j++)  f[islow*dof+j] += ctx->flux[j]/hs;
        islow++;
      }
    } else if (i == fs+1) {
      u = &ctx->u[0];
      alpha[0] = hs*hs/(hf+hs)/(hf+hs+hs);
      gamma[0] = hs*(hf+hs)/(hs+hs)/(hf+hs+hs);
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j] - x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr = (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      if (i > xs) {
        for (j=0; j<dof; j++) f[(islow-1)*dof+j] -= ctx->flux[j]/hs;
      }
      if (i < xs+xm) {
        for (j=0; j<dof; j++) f[islow*dof+j] += ctx->flux[j]/hs;
        islow++;
      }
    } else if (i > fs+1) {
      u = &ctx->u[0];
      alpha[0] = 1.0/6.0;
      gamma[0] = 1.0/3.0;
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j] - x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr = (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      if (i > xs) {
        for (j=0; j<dof; j++) f[(islow-1)*dof+j] -= ctx->flux[j]/hs;
      }
      if (i < xs+xm) {
        for (j=0; j<dof; j++) f[islow*dof+j] += ctx->flux[j]/hs;
        islow++;
      }
    }
  }
  ierr = DMDAVecRestoreArray(da,Xloc,&x);CHKERRQ(ierr);
  ierr = VecRestoreArray(F,&f);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(da,&Xloc);CHKERRQ(ierr);
  ierr = PetscFree4(r,min,alpha,gamma);CHKERRQ(ierr);
  PetscFunctionReturn(0);
 }

static PetscErrorCode FVRHSFunctionfast(TS ts,PetscReal time,Vec X,Vec F,void *vctx)
{
  FVCtx          *ctx = (FVCtx*)vctx;
  PetscErrorCode ierr;
  PetscInt       i,j,Mx,dof,xs,xm,ifast = 0,sf = ctx->sf,fs = ctx->fs;
  PetscReal      hf,hs;
  PetscScalar    *x,*f,*r,*min,*alpha,*gamma;
  Vec            Xloc;
  DM             da;

  PetscFunctionBeginUser;
  ierr = TSGetDM(ts,&da);CHKERRQ(ierr);
  ierr = DMGetLocalVector(da,&Xloc);CHKERRQ(ierr);                          /* Xloc contains ghost points                                     */
  ierr = DMDAGetInfo(da,0,&Mx,0,0,0,0,0,&dof,0,0,0,0,0);CHKERRQ(ierr);   /* Mx is the number of center points                              */
  hs   = (ctx->xmax-ctx->xmin)/2.0*(ctx->hratio+1.0)/Mx;
  hf   = (ctx->xmax-ctx->xmin)/2.0*(1.0+1.0/ctx->hratio)/Mx;
  ierr = DMGlobalToLocalBegin(da,X,INSERT_VALUES,Xloc);CHKERRQ(ierr);       /* X is solution vector which does not contain ghost points       */
  ierr = DMGlobalToLocalEnd(da,X,INSERT_VALUES,Xloc);CHKERRQ(ierr);
  ierr = VecZeroEntries(F);CHKERRQ(ierr);                                   /* F is the right hand side function corresponds to center points */
  ierr = DMDAVecGetArray(da,Xloc,&x);CHKERRQ(ierr);
  ierr = VecGetArray(F,&f);CHKERRQ(ierr);
  ierr = DMDAGetCorners(da,&xs,0,0,&xm,0,0);CHKERRQ(ierr);
  ierr = PetscMalloc4(dof,&r,dof,&min,dof,&alpha,dof,&gamma);CHKERRQ(ierr);

  if (ctx->bctype == FVBC_OUTFLOW) {
    for (i=xs-2; i<0; i++) {
      for (j=0; j<dof; j++) x[i*dof+j] = x[j];
    }
    for (i=Mx; i<xs+xm+2; i++) {
      for (j=0; j<dof; j++) x[i*dof+j] = x[(xs+xm-1)*dof+j];
    }
  }

  for (i=xs; i<xs+xm+1; i++) {
    PetscReal   maxspeed;
    PetscScalar *u;
    if (i == sf) {
      u = &ctx->u[0];
      alpha[0] = hs*hf/(hs+hs)/(hs+hs+hf);
      gamma[0] = hs*(hs+hs)/(hs+hf)/(hs+hs+hf);
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j]-x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr = (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      if (i < xs+xm) {
        for (j=0; j<dof; j++) f[ifast*dof+j] += ctx->flux[j]/hf;
        ifast++;
      }
    } else if (i == sf+1) {
      u = &ctx->u[0];
      alpha[0] = hf*hf/(hs+hf)/(hs+hf+hf);
      gamma[0] = hf*(hs+hf)/(hf+hf)/(hs+hf+hf);
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j]-x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr = (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      if (i > xs) {
        for (j=0; j<dof; j++) f[(ifast-1)*dof+j] -= ctx->flux[j]/hf;
      }
      if (i < xs+xm) {
        for (j=0; j<dof; j++) f[ifast*dof+j] += ctx->flux[j]/hf;
        ifast++;
      }
    } else if (i > sf+1 && i < fs) {
      u = &ctx->u[0];
      alpha[0] = 1.0/6.0;
      gamma[0] = 1.0/3.0;
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j]-x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr = (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      if (i > xs) {
        for (j=0; j<dof; j++) f[(ifast-1)*dof+j] -= ctx->flux[j]/hf;
      }
      if (i < xs+xm) {
        for (j=0; j<dof; j++) f[ifast*dof+j] += ctx->flux[j]/hf;
        ifast++;
      }
    } else if (i == fs) {
      u = &ctx->u[0];
      alpha[0] = hf*hs/(hf+hf)/(hf+hf+hs);
      gamma[0] = hf*(hf+hf)/(hf+hs)/(hf+hf+hs);
      for (j=0; j<dof; j++) {
        r[j] = (x[i*dof+j]-x[(i-1)*dof+j])/(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
        min[j] = PetscMin(r[j],2.0);
        u[j] = x[(i-1)*dof+j]+PetscMax(0,PetscMin(min[j],alpha[0]+gamma[0]*r[j]))*(x[(i-1)*dof+j]-x[(i-2)*dof+j]);
      }
      ierr =  (*ctx->physics.flux)(ctx->physics.user,u,ctx->flux,&maxspeed);CHKERRQ(ierr);
      if (i > xs) {
        for (j=0; j<dof; j++) f[(ifast-1)*dof+j] -= ctx->flux[j]/hf;
      }
    }
  }
  ierr = DMDAVecRestoreArray(da,Xloc,&x);CHKERRQ(ierr);
  ierr = VecRestoreArray(F,&f);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(da,&Xloc);CHKERRQ(ierr);
  ierr = PetscFree4(r,min,alpha,gamma);CHKERRQ(ierr);
  PetscFunctionReturn(0);
 }

/* --------------------------------- Finite Volume Solver for slow components ----------------------------------- */

PetscErrorCode FVSample(FVCtx *ctx,DM da,PetscReal time,Vec U)
{
  PetscErrorCode ierr;
  PetscScalar    *u,*uj,xj,xi;
  PetscInt       i,j,k,dof,xs,xm,Mx,count_slow,count_fast;
  const PetscInt N=200;

  PetscFunctionBeginUser;
  PetscCheck(ctx->physics.sample,PETSC_COMM_SELF,PETSC_ERR_SUP,"Physics has not provided a sampling function");
  ierr = DMDAGetInfo(da,0,&Mx,0,0,0,0,0,&dof,0,0,0,0,0);CHKERRQ(ierr);
  ierr = DMDAGetCorners(da,&xs,0,0,&xm,0,0);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(da,U,&u);CHKERRQ(ierr);
  ierr = PetscMalloc1(dof,&uj);CHKERRQ(ierr);
  const PetscReal hs = (ctx->xmax-ctx->xmin)/2.0*(ctx->hratio+1.0)/Mx;
  const PetscReal hf = (ctx->xmax-ctx->xmin)/2.0*(1.0+1.0/ctx->hratio)/Mx;
  count_slow = Mx/(1+ctx->hratio);
  count_fast = Mx-count_slow;
  for (i=xs; i<xs+xm; i++) {
    if (i*hs+0.5*hs<(ctx->xmax-ctx->xmin)*0.25) {
      xi = ctx->xmin+0.5*hs+i*hs;
      /* Integrate over cell i using trapezoid rule with N points. */
      for (k=0; k<dof; k++) u[i*dof+k] = 0;
      for (j=0; j<N+1; j++) {
        xj = xi+hs*(j-N/2)/(PetscReal)N;
        ierr = (*ctx->physics.sample)(ctx->physics.user,ctx->initial,ctx->bctype,ctx->xmin,ctx->xmax,time,xj,uj);CHKERRQ(ierr);
        for (k=0; k<dof; k++) u[i*dof+k] += ((j==0 || j==N) ? 0.5 : 1.0)*uj[k]/N;
      }
    } else if ((ctx->xmax-ctx->xmin)*0.25+(i-count_slow/2)*hf+0.5*hf<(ctx->xmax-ctx->xmin)*0.75) {
      xi = ctx->xmin+(ctx->xmax-ctx->xmin)*0.25+0.5*hf+(i-count_slow/2)*hf;
      /* Integrate over cell i using trapezoid rule with N points. */
      for (k=0; k<dof; k++) u[i*dof+k] = 0;
      for (j=0; j<N+1; j++) {
        xj = xi+hf*(j-N/2)/(PetscReal)N;
        ierr = (*ctx->physics.sample)(ctx->physics.user,ctx->initial,ctx->bctype,ctx->xmin,ctx->xmax,time,xj,uj);CHKERRQ(ierr);
        for (k=0; k<dof; k++) u[i*dof+k] += ((j==0 || j==N) ? 0.5 : 1.0)*uj[k]/N;
      }
    } else {
      xi = ctx->xmin+(ctx->xmax-ctx->xmin)*0.75+0.5*hs+(i-count_slow/2-count_fast)*hs;
      /* Integrate over cell i using trapezoid rule with N points. */
      for (k=0; k<dof; k++) u[i*dof+k] = 0;
      for (j=0; j<N+1; j++) {
        xj = xi+hs*(j-N/2)/(PetscReal)N;
        ierr = (*ctx->physics.sample)(ctx->physics.user,ctx->initial,ctx->bctype,ctx->xmin,ctx->xmax,time,xj,uj);CHKERRQ(ierr);
        for (k=0; k<dof; k++) u[i*dof+k] += ((j==0 || j==N) ? 0.5 : 1.0)*uj[k]/N;
      }
    }
  }
  ierr = DMDAVecRestoreArray(da,U,&u);CHKERRQ(ierr);
  ierr = PetscFree(uj);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode SolutionStatsView(DM da,Vec X,PetscViewer viewer)
{
  PetscErrorCode    ierr;
  PetscReal         xmin,xmax;
  PetscScalar       sum,tvsum,tvgsum;
  const PetscScalar *x;
  PetscInt          imin,imax,Mx,i,j,xs,xm,dof;
  Vec               Xloc;
  PetscBool         iascii;

  PetscFunctionBeginUser;
  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&iascii);CHKERRQ(ierr);
  if (iascii) {
    /* PETSc lacks a function to compute total variation norm (difficult in multiple dimensions), we do it here */
    ierr  = DMGetLocalVector(da,&Xloc);CHKERRQ(ierr);
    ierr  = DMGlobalToLocalBegin(da,X,INSERT_VALUES,Xloc);CHKERRQ(ierr);
    ierr  = DMGlobalToLocalEnd(da,X,INSERT_VALUES,Xloc);CHKERRQ(ierr);
    ierr  = DMDAVecGetArrayRead(da,Xloc,(void*)&x);CHKERRQ(ierr);
    ierr  = DMDAGetCorners(da,&xs,0,0,&xm,0,0);CHKERRQ(ierr);
    ierr  = DMDAGetInfo(da,0,&Mx,0,0,0,0,0,&dof,0,0,0,0,0);CHKERRQ(ierr);
    tvsum = 0;
    for (i=xs; i<xs+xm; i++) {
      for (j=0; j<dof; j++) tvsum += PetscAbsScalar(x[i*dof+j]-x[(i-1)*dof+j]);
    }
    ierr = MPI_Allreduce(&tvsum,&tvgsum,1,MPIU_SCALAR,MPIU_SUM,PetscObjectComm((PetscObject)da));CHKERRMPI(ierr);
    ierr = DMDAVecRestoreArrayRead(da,Xloc,(void*)&x);CHKERRQ(ierr);
    ierr = DMRestoreLocalVector(da,&Xloc);CHKERRQ(ierr);

    ierr = VecMin(X,&imin,&xmin);CHKERRQ(ierr);
    ierr = VecMax(X,&imax,&xmax);CHKERRQ(ierr);
    ierr = VecSum(X,&sum);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"Solution range [%g,%g] with minimum at %D, mean %g, ||x||_TV %g\n",(double)xmin,(double)xmax,imin,(double)(sum/Mx),(double)(tvgsum/Mx));CHKERRQ(ierr);
  } else SETERRQ(PETSC_COMM_SELF,PETSC_ERR_SUP,"Viewer type not supported");
  PetscFunctionReturn(0);
}

static PetscErrorCode SolutionErrorNorms(FVCtx *ctx,DM da,PetscReal t,Vec X,PetscReal *nrm1)
{
  PetscErrorCode    ierr;
  Vec               Y;
  PetscInt          i,Mx,count_slow=0,count_fast=0;
  const PetscScalar *ptr_X,*ptr_Y;

  PetscFunctionBeginUser;
  ierr = VecGetSize(X,&Mx);CHKERRQ(ierr);
  ierr = VecDuplicate(X,&Y);CHKERRQ(ierr);
  ierr = FVSample(ctx,da,t,Y);CHKERRQ(ierr);
  const PetscReal hs = (ctx->xmax-ctx->xmin)/2.0*(ctx->hratio+1.0)/Mx;
  const PetscReal hf = (ctx->xmax-ctx->xmin)/2.0*(1.0+1.0/ctx->hratio)/Mx;
  count_slow = (PetscReal)Mx/(1.0+ctx->hratio);
  count_fast = Mx-count_slow;
  ierr = VecGetArrayRead(X,&ptr_X);CHKERRQ(ierr);
  ierr = VecGetArrayRead(Y,&ptr_Y);CHKERRQ(ierr);
  for (i=0; i<Mx; i++) {
    if (i < count_slow/2 || i > count_slow/2+count_fast-1) *nrm1 +=  hs*PetscAbs(ptr_X[i]-ptr_Y[i]);
    else *nrm1 += hf*PetscAbs(ptr_X[i]-ptr_Y[i]);
  }
  ierr = VecRestoreArrayRead(X,&ptr_X);CHKERRQ(ierr);
  ierr = VecRestoreArrayRead(Y,&ptr_Y);CHKERRQ(ierr);
  ierr = VecDestroy(&Y);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

int main(int argc,char *argv[])
{
  char              physname[256] = "advect",final_fname[256] = "solution.m";
  PetscFunctionList physics = 0;
  MPI_Comm          comm;
  TS                ts;
  DM                da;
  Vec               X,X0,R;
  FVCtx             ctx;
  PetscInt          i,k,dof,xs,xm,Mx,draw = 0,count_slow,count_fast,islow = 0,ifast = 0,*index_slow,*index_fast;
  PetscBool         view_final = PETSC_FALSE;
  PetscReal         ptime;
  PetscErrorCode    ierr;

  ierr = PetscInitialize(&argc,&argv,0,help);if (ierr) return ierr;
  comm = PETSC_COMM_WORLD;
  ierr = PetscMemzero(&ctx,sizeof(ctx));CHKERRQ(ierr);

  /* Register physical models to be available on the command line */
  ierr = PetscFunctionListAdd(&physics,"advect",PhysicsCreate_Advect);CHKERRQ(ierr);

  ctx.comm = comm;
  ctx.cfl  = 0.9;
  ctx.bctype = FVBC_PERIODIC;
  ctx.xmin = -1.0;
  ctx.xmax = 1.0;
  ierr = PetscOptionsBegin(comm,NULL,"Finite Volume solver options","");CHKERRQ(ierr);
  ierr = PetscOptionsReal("-xmin","X min","",ctx.xmin,&ctx.xmin,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsReal("-xmax","X max","",ctx.xmax,&ctx.xmax,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsInt("-draw","Draw solution vector, bitwise OR of (1=initial,2=final,4=final error)","",draw,&draw,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsString("-view_final","Write final solution in ASCII MATLAB format to given file name","",final_fname,final_fname,sizeof(final_fname),&view_final);CHKERRQ(ierr);
  ierr = PetscOptionsInt("-initial","Initial condition (depends on the physics)","",ctx.initial,&ctx.initial,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsBool("-exact","Compare errors with exact solution","",ctx.exact,&ctx.exact,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsBool("-simulation","Compare errors with reference solution","",ctx.simulation,&ctx.simulation,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsReal("-cfl","CFL number to time step at","",ctx.cfl,&ctx.cfl,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsEnum("-bc_type","Boundary condition","",FVBCTypes,(PetscEnum)ctx.bctype,(PetscEnum*)&ctx.bctype,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsInt("-hratio","Spacing ratio","",ctx.hratio,&ctx.hratio,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsEnd();CHKERRQ(ierr);

  /* Choose the physics from the list of registered models */
  {
    PetscErrorCode (*r)(FVCtx*);
    ierr = PetscFunctionListFind(physics,physname,&r);CHKERRQ(ierr);
    PetscCheck(r,PETSC_COMM_SELF,PETSC_ERR_ARG_UNKNOWN_TYPE,"Physics '%s' not found",physname);
    /* Create the physics, will set the number of fields and their names */
    ierr = (*r)(&ctx);CHKERRQ(ierr);
  }

  /* Create a DMDA to manage the parallel grid */
  ierr = DMDACreate1d(comm,DM_BOUNDARY_PERIODIC,50,ctx.physics.dof,2,NULL,&da);CHKERRQ(ierr);
  ierr = DMSetFromOptions(da);CHKERRQ(ierr);
  ierr = DMSetUp(da);CHKERRQ(ierr);
  /* Inform the DMDA of the field names provided by the physics. */
  /* The names will be shown in the title bars when run with -ts_monitor_draw_solution */
  for (i=0; i<ctx.physics.dof; i++) {
    ierr = DMDASetFieldName(da,i,ctx.physics.fieldname[i]);CHKERRQ(ierr);
  }
  ierr = DMDAGetInfo(da,0,&Mx,0,0,0,0,0,&dof,0,0,0,0,0);CHKERRQ(ierr);
  ierr = DMDAGetCorners(da,&xs,0,0,&xm,0,0);CHKERRQ(ierr);

  /* Set coordinates of cell centers */
  ierr = DMDASetUniformCoordinates(da,ctx.xmin+0.5*(ctx.xmax-ctx.xmin)/Mx,ctx.xmax+0.5*(ctx.xmax-ctx.xmin)/Mx,0,0,0,0);CHKERRQ(ierr);

  /* Allocate work space for the Finite Volume solver (so it doesn't have to be reallocated on each function evaluation) */
  ierr = PetscMalloc3(dof,&ctx.u,dof,&ctx.flux,dof,&ctx.speeds);CHKERRQ(ierr);

  /* Create a vector to store the solution and to save the initial state */
  ierr = DMCreateGlobalVector(da,&X);CHKERRQ(ierr);
  ierr = VecDuplicate(X,&X0);CHKERRQ(ierr);
  ierr = VecDuplicate(X,&R);CHKERRQ(ierr);

  /* create index for slow parts and fast parts*/
  count_slow = Mx/(1+ctx.hratio);
  PetscCheckFalse(count_slow%2,PETSC_COMM_WORLD,PETSC_ERR_USER,"Please adjust grid size Mx (-da_grid_x) and hratio (-hratio) so that Mx/(1+hartio) is even");
  count_fast = Mx-count_slow;
  ctx.sf = count_slow/2;
  ctx.fs = ctx.sf + count_fast;
  ierr = PetscMalloc1(xm*dof,&index_slow);CHKERRQ(ierr);
  ierr = PetscMalloc1(xm*dof,&index_fast);CHKERRQ(ierr);
  for (i=xs; i<xs+xm; i++) {
    if (i < count_slow/2 || i > count_slow/2+count_fast-1)
      for (k=0; k<dof; k++) index_slow[islow++] = i*dof+k;
    else
      for (k=0; k<dof; k++) index_fast[ifast++] = i*dof+k;
  }
  ierr = ISCreateGeneral(PETSC_COMM_WORLD,islow,index_slow,PETSC_COPY_VALUES,&ctx.iss);CHKERRQ(ierr);
  ierr = ISCreateGeneral(PETSC_COMM_WORLD,ifast,index_fast,PETSC_COPY_VALUES,&ctx.isf);CHKERRQ(ierr);

  /* Create a time-stepping object */
  ierr = TSCreate(comm,&ts);CHKERRQ(ierr);
  ierr = TSSetDM(ts,da);CHKERRQ(ierr);
  ierr = TSSetRHSFunction(ts,R,FVRHSFunction,&ctx);CHKERRQ(ierr);
  ierr = TSRHSSplitSetIS(ts,"slow",ctx.iss);CHKERRQ(ierr);
  ierr = TSRHSSplitSetIS(ts,"fast",ctx.isf);CHKERRQ(ierr);
  ierr = TSRHSSplitSetRHSFunction(ts,"slow",NULL,FVRHSFunctionslow,&ctx);CHKERRQ(ierr);
  ierr = TSRHSSplitSetRHSFunction(ts,"fast",NULL,FVRHSFunctionfast,&ctx);CHKERRQ(ierr);

  ierr = TSSetType(ts,TSMPRK);CHKERRQ(ierr);
  ierr = TSSetMaxTime(ts,10);CHKERRQ(ierr);
  ierr = TSSetExactFinalTime(ts,TS_EXACTFINALTIME_STEPOVER);CHKERRQ(ierr);

  /* Compute initial conditions and starting time step */
  ierr = FVSample(&ctx,da,0,X0);CHKERRQ(ierr);
  ierr = FVRHSFunction(ts,0,X0,X,(void*)&ctx);CHKERRQ(ierr); /* Initial function evaluation, only used to determine max speed */
  ierr = VecCopy(X0,X);CHKERRQ(ierr);                        /* The function value was not used so we set X=X0 again */
  ierr = TSSetTimeStep(ts,ctx.cfl/ctx.cfl_idt);CHKERRQ(ierr);
  ierr = TSSetFromOptions(ts);CHKERRQ(ierr); /* Take runtime options */
  ierr = SolutionStatsView(da,X,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  {
    PetscInt          steps;
    PetscScalar       mass_initial,mass_final,mass_difference,mass_differenceg;
    const PetscScalar *ptr_X,*ptr_X0;
    const PetscReal   hs  = (ctx.xmax-ctx.xmin)/2.0/count_slow;
    const PetscReal   hf  = (ctx.xmax-ctx.xmin)/2.0/count_fast;
    ierr = TSSolve(ts,X);CHKERRQ(ierr);
    ierr = TSGetSolveTime(ts,&ptime);CHKERRQ(ierr);
    ierr = TSGetStepNumber(ts,&steps);CHKERRQ(ierr);
    /* calculate the total mass at initial time and final time */
    mass_initial = 0.0;
    mass_final   = 0.0;
    ierr = DMDAVecGetArrayRead(da,X0,(void*)&ptr_X0);CHKERRQ(ierr);
    ierr = DMDAVecGetArrayRead(da,X,(void*)&ptr_X);CHKERRQ(ierr);
    for (i=xs; i<xs+xm; i++) {
      if (i < ctx.sf || i > ctx.fs-1) {
        for (k=0; k<dof; k++) {
          mass_initial = mass_initial+hs*ptr_X0[i*dof+k];
          mass_final = mass_final+hs*ptr_X[i*dof+k];
        }
      } else {
        for (k=0; k<dof; k++) {
          mass_initial = mass_initial+hf*ptr_X0[i*dof+k];
          mass_final = mass_final+hf*ptr_X[i*dof+k];
        }
      }
    }
    ierr = DMDAVecRestoreArrayRead(da,X0,(void*)&ptr_X0);CHKERRQ(ierr);
    ierr = DMDAVecRestoreArrayRead(da,X,(void*)&ptr_X);CHKERRQ(ierr);
    mass_difference = mass_final-mass_initial;
    ierr = MPI_Allreduce(&mass_difference,&mass_differenceg,1,MPIU_SCALAR,MPIU_SUM,comm);CHKERRMPI(ierr);
    ierr = PetscPrintf(comm,"Mass difference %g\n",(double)mass_differenceg);CHKERRQ(ierr);
    ierr = PetscPrintf(comm,"Final time %g, steps %D\n",(double)ptime,steps);CHKERRQ(ierr);
    if (ctx.exact) {
      PetscReal nrm1 = 0;
      ierr = SolutionErrorNorms(&ctx,da,ptime,X,&nrm1);CHKERRQ(ierr);
      ierr = PetscPrintf(comm,"Error ||x-x_e||_1 %g\n",(double)nrm1);CHKERRQ(ierr);
    }
    if (ctx.simulation) {
      PetscReal         nrm1 = 0;
      PetscViewer       fd;
      char              filename[PETSC_MAX_PATH_LEN] = "binaryoutput";
      Vec               XR;
      PetscBool         flg;
      const PetscScalar *ptr_XR;
      ierr = PetscOptionsGetString(NULL,NULL,"-f",filename,sizeof(filename),&flg);CHKERRQ(ierr);
      PetscCheck(flg,PETSC_COMM_WORLD,PETSC_ERR_USER,"Must indicate binary file with the -f option");
      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,filename,FILE_MODE_READ,&fd);CHKERRQ(ierr);
      ierr = VecDuplicate(X0,&XR);CHKERRQ(ierr);
      ierr = VecLoad(XR,fd);CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&fd);CHKERRQ(ierr);
      ierr = VecGetArrayRead(X,&ptr_X);CHKERRQ(ierr);
      ierr = VecGetArrayRead(XR,&ptr_XR);CHKERRQ(ierr);
      for (i=0; i<Mx; i++) {
        if (i < count_slow/2 || i > count_slow/2+count_fast-1) nrm1 = nrm1 + hs*PetscAbs(ptr_X[i]-ptr_XR[i]);
        else nrm1 = nrm1 + hf*PetscAbs(ptr_X[i]-ptr_XR[i]);
      }
      ierr = VecRestoreArrayRead(X,&ptr_X);CHKERRQ(ierr);
      ierr = VecRestoreArrayRead(XR,&ptr_XR);CHKERRQ(ierr);
      ierr = PetscPrintf(comm,"Error ||x-x_e||_1 %g\n",(double)nrm1);CHKERRQ(ierr);
      ierr = VecDestroy(&XR);CHKERRQ(ierr);
    }
  }

  ierr = SolutionStatsView(da,X,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  if (draw & 0x1) { ierr = VecView(X0,PETSC_VIEWER_DRAW_WORLD);CHKERRQ(ierr); }
  if (draw & 0x2) { ierr = VecView(X,PETSC_VIEWER_DRAW_WORLD);CHKERRQ(ierr); }
  if (draw & 0x4) {
    Vec Y;
    ierr = VecDuplicate(X,&Y);CHKERRQ(ierr);
    ierr = FVSample(&ctx,da,ptime,Y);CHKERRQ(ierr);
    ierr = VecAYPX(Y,-1,X);CHKERRQ(ierr);
    ierr = VecView(Y,PETSC_VIEWER_DRAW_WORLD);CHKERRQ(ierr);
    ierr = VecDestroy(&Y);CHKERRQ(ierr);
  }

  if (view_final) {
    PetscViewer viewer;
    ierr = PetscViewerASCIIOpen(PETSC_COMM_WORLD,final_fname,&viewer);CHKERRQ(ierr);
    ierr = PetscViewerPushFormat(viewer,PETSC_VIEWER_ASCII_MATLAB);CHKERRQ(ierr);
    ierr = VecView(X,viewer);CHKERRQ(ierr);
    ierr = PetscViewerPopFormat(viewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  }

  /* Clean up */
  ierr = (*ctx.physics.destroy)(ctx.physics.user);CHKERRQ(ierr);
  for (i=0; i<ctx.physics.dof; i++) {ierr = PetscFree(ctx.physics.fieldname[i]);CHKERRQ(ierr);}
  ierr = PetscFree3(ctx.u,ctx.flux,ctx.speeds);CHKERRQ(ierr);
  ierr = ISDestroy(&ctx.iss);CHKERRQ(ierr);
  ierr = ISDestroy(&ctx.isf);CHKERRQ(ierr);
  ierr = VecDestroy(&X);CHKERRQ(ierr);
  ierr = VecDestroy(&X0);CHKERRQ(ierr);
  ierr = VecDestroy(&R);CHKERRQ(ierr);
  ierr = DMDestroy(&da);CHKERRQ(ierr);
  ierr = TSDestroy(&ts);CHKERRQ(ierr);
  ierr = PetscFree(index_slow);CHKERRQ(ierr);
  ierr = PetscFree(index_fast);CHKERRQ(ierr);
  ierr = PetscFunctionListDestroy(&physics);CHKERRQ(ierr);
  ierr = PetscFinalize();
  return ierr;
}

/*TEST

    build:
      requires: !complex

    test:
      args: -da_grid_x 60 -initial 7 -xmin -1 -xmax 1 -hratio 2 -ts_dt 0.025 -ts_max_steps 24 -ts_type rk -ts_rk_type 2a -ts_rk_dtratio 2 -ts_rk_multirate -ts_use_splitrhsfunction 0

    test:
      suffix: 2
      args: -da_grid_x 60 -initial 7 -xmin -1 -xmax 1 -hratio 2 -ts_dt 0.025 -ts_max_steps 24 -ts_type rk -ts_rk_type 2a -ts_rk_dtratio 2 -ts_rk_multirate -ts_use_splitrhsfunction 1
      output_file: output/ex7_1.out

    test:
      suffix: 3
      args: -da_grid_x 60 -initial 7 -xmin -1 -xmax 1 -hratio 2 -ts_dt 0.025 -ts_max_steps 24 -ts_type mprk -ts_mprk_type 2a22 -ts_use_splitrhsfunction 0

    test:
      suffix: 4
      args: -da_grid_x 60 -initial 7 -xmin -1 -xmax 1 -hratio 2 -ts_dt 0.025 -ts_max_steps 24 -ts_type mprk -ts_mprk_type 2a22 -ts_use_splitrhsfunction 1
      output_file: output/ex7_3.out

    test:
      suffix: 5
      nsize: 2
      args: -da_grid_x 60 -initial 7 -xmin -1 -xmax 1 -hratio 2 -ts_dt 0.025 -ts_max_steps 24 -ts_type mprk -ts_mprk_type 2a22 -ts_use_splitrhsfunction 1
      output_file: output/ex7_3.out
TEST*/
