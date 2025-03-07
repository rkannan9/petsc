static char help[] = "Power grid stability analysis of WECC 9 bus system.\n\
This example is based on the 9-bus (node) example given in the book Power\n\
Systems Dynamics and Stability (Chapter 7) by P. Sauer and M. A. Pai.\n\
The power grid in this example consists of 9 buses (nodes), 3 generators,\n\
3 loads, and 9 transmission lines. The network equations are written\n\
in current balance form using rectangular coordinates.\n\n";

/*
   The equations for the stability analysis are described by the DAE

   \dot{x} = f(x,y,t)
     0     = g(x,y,t)

   where the generators are described by differential equations, while the algebraic
   constraints define the network equations.

   The generators are modeled with a 4th order differential equation describing the electrical
   and mechanical dynamics. Each generator also has an exciter system modeled by 3rd order
   diff. eqns. describing the exciter, voltage regulator, and the feedback stabilizer
   mechanism.

   The network equations are described by nodal current balance equations.
    I(x,y) - Y*V = 0

   where:
    I(x,y) is the current injected from generators and loads.
      Y    is the admittance matrix, and
      V    is the voltage vector
*/

/*
   Include "petscts.h" so that we can use TS solvers.  Note that this
   file automatically includes:
     petscsys.h       - base PETSc routines   petscvec.h - vectors
     petscmat.h - matrices
     petscis.h     - index sets            petscksp.h - Krylov subspace methods
     petscviewer.h - viewers               petscpc.h  - preconditioners
     petscksp.h   - linear solvers
*/

#include <petscts.h>
#include <petscdm.h>
#include <petscdmda.h>
#include <petscdmcomposite.h>

#define freq 60
#define w_s  (2 * PETSC_PI * freq)

/* Sizes and indices */
const PetscInt nbus    = 9;         /* Number of network buses */
const PetscInt ngen    = 3;         /* Number of generators */
const PetscInt nload   = 3;         /* Number of loads */
const PetscInt gbus[3] = {0, 1, 2}; /* Buses at which generators are incident */
const PetscInt lbus[3] = {4, 5, 7}; /* Buses at which loads are incident */

/* Generator real and reactive powers (found via loadflow) */
const PetscScalar PG[3] = {0.716786142395021, 1.630000000000000, 0.850000000000000};
const PetscScalar QG[3] = {0.270702180178785, 0.066120127797275, -0.108402221791588};
/* Generator constants */
const PetscScalar H[3]    = {23.64, 6.4, 3.01};       /* Inertia constant */
const PetscScalar Rs[3]   = {0.0, 0.0, 0.0};          /* Stator Resistance */
const PetscScalar Xd[3]   = {0.146, 0.8958, 1.3125};  /* d-axis reactance */
const PetscScalar Xdp[3]  = {0.0608, 0.1198, 0.1813}; /* d-axis transient reactance */
const PetscScalar Xq[3]   = {0.4360, 0.8645, 1.2578}; /* q-axis reactance Xq(1) set to 0.4360, value given in text 0.0969 */
const PetscScalar Xqp[3]  = {0.0969, 0.1969, 0.25};   /* q-axis transient reactance */
const PetscScalar Td0p[3] = {8.96, 6.0, 5.89};        /* d-axis open circuit time constant */
const PetscScalar Tq0p[3] = {0.31, 0.535, 0.6};       /* q-axis open circuit time constant */
PetscScalar       M[3];                               /* M = 2*H/w_s */
PetscScalar       D[3];                               /* D = 0.1*M */

PetscScalar TM[3]; /* Mechanical Torque */
/* Exciter system constants */
const PetscScalar KA[3]    = {20.0, 20.0, 20.0};    /* Voltage regulartor gain constant */
const PetscScalar TA[3]    = {0.2, 0.2, 0.2};       /* Voltage regulator time constant */
const PetscScalar KE[3]    = {1.0, 1.0, 1.0};       /* Exciter gain constant */
const PetscScalar TE[3]    = {0.314, 0.314, 0.314}; /* Exciter time constant */
const PetscScalar KF[3]    = {0.063, 0.063, 0.063}; /* Feedback stabilizer gain constant */
const PetscScalar TF[3]    = {0.35, 0.35, 0.35};    /* Feedback stabilizer time constant */
const PetscScalar k1[3]    = {0.0039, 0.0039, 0.0039};
const PetscScalar k2[3]    = {1.555, 1.555, 1.555}; /* k1 and k2 for calculating the saturation function SE = k1*exp(k2*Efd) */
const PetscScalar VRMIN[3] = {-4.0, -4.0, -4.0};
const PetscScalar VRMAX[3] = {7.0, 7.0, 7.0};
PetscInt          VRatmin[3];
PetscInt          VRatmax[3];

PetscScalar Vref[3];
/* Load constants
  We use a composite load model that describes the load and reactive powers at each time instant as follows
  P(t) = \sum\limits_{i=0}^ld_nsegsp \ld_alphap_i*P_D0(\frac{V_m(t)}{V_m0})^\ld_betap_i
  Q(t) = \sum\limits_{i=0}^ld_nsegsq \ld_alphaq_i*Q_D0(\frac{V_m(t)}{V_m0})^\ld_betaq_i
  where
    ld_nsegsp,ld_nsegsq - Number of individual load models for real and reactive power loads
    ld_alphap,ld_alphap - Percentage contribution (weights) or loads
    P_D0                - Real power load
    Q_D0                - Reactive power load
    V_m(t)              - Voltage magnitude at time t
    V_m0                - Voltage magnitude at t = 0
    ld_betap, ld_betaq  - exponents describing the load model for real and reactive part

    Note: All loads have the same characteristic currently.
*/
const PetscScalar PD0[3]       = {1.25, 0.9, 1.0};
const PetscScalar QD0[3]       = {0.5, 0.3, 0.35};
const PetscInt    ld_nsegsp[3] = {3, 3, 3};
const PetscScalar ld_alphap[3] = {1.0, 0.0, 0.0};
const PetscScalar ld_betap[3]  = {2.0, 1.0, 0.0};
const PetscInt    ld_nsegsq[3] = {3, 3, 3};
const PetscScalar ld_alphaq[3] = {1.0, 0.0, 0.0};
const PetscScalar ld_betaq[3]  = {2.0, 1.0, 0.0};

typedef struct {
  DM          dmgen, dmnet;        /* DMs to manage generator and network subsystem */
  DM          dmpgrid;             /* Composite DM to manage the entire power grid */
  Mat         Ybus;                /* Network admittance matrix */
  Vec         V0;                  /* Initial voltage vector (Power flow solution) */
  PetscReal   tfaulton, tfaultoff; /* Fault on and off times */
  PetscInt    faultbus;            /* Fault bus */
  PetscScalar Rfault;
  PetscReal   t0, tmax;
  PetscInt    neqs_gen, neqs_net, neqs_pgrid;
  Mat         Sol; /* Matrix to save solution at each time step */
  PetscInt    stepnum;
  PetscReal   t;
  SNES        snes_alg;
  IS          is_diff;      /* indices for differential equations */
  IS          is_alg;       /* indices for algebraic equations */
  PetscBool   setisdiff;    /* TS computes truncation error based only on the differential variables */
  PetscBool   semiexplicit; /* If the flag is set then a semi-explicit method is used using TSRK */
} Userctx;

/*
  The first two events are for fault on and off, respectively. The following events are
  to check the min/max limits on the state variable VR. A non windup limiter is used for
  the VR limits.
*/
PetscErrorCode EventFunction(TS ts, PetscReal t, Vec X, PetscReal *fvalue, void *ctx)
{
  Userctx           *user = (Userctx *)ctx;
  Vec                Xgen, Xnet;
  PetscInt           i, idx = 0;
  const PetscScalar *xgen, *xnet;
  PetscScalar        Efd, RF, VR, Vr, Vi, Vm;

  PetscFunctionBegin;

  PetscCall(DMCompositeGetLocalVectors(user->dmpgrid, &Xgen, &Xnet));
  PetscCall(DMCompositeScatter(user->dmpgrid, X, Xgen, Xnet));

  PetscCall(VecGetArrayRead(Xgen, &xgen));
  PetscCall(VecGetArrayRead(Xnet, &xnet));

  /* Event for fault-on time */
  fvalue[0] = t - user->tfaulton;
  /* Event for fault-off time */
  fvalue[1] = t - user->tfaultoff;

  for (i = 0; i < ngen; i++) {
    Efd = xgen[idx + 6];
    RF  = xgen[idx + 7];
    VR  = xgen[idx + 8];

    Vr = xnet[2 * gbus[i]];     /* Real part of generator terminal voltage */
    Vi = xnet[2 * gbus[i] + 1]; /* Imaginary part of the generator terminal voltage */
    Vm = PetscSqrtScalar(Vr * Vr + Vi * Vi);

    if (!VRatmax[i]) {
      fvalue[2 + 2 * i] = PetscRealPart(VRMAX[i] - VR);
    } else {
      fvalue[2 + 2 * i] = PetscRealPart((VR - KA[i] * RF + KA[i] * KF[i] * Efd / TF[i] - KA[i] * (Vref[i] - Vm)) / TA[i]);
    }
    if (!VRatmin[i]) {
      fvalue[2 + 2 * i + 1] = PetscRealPart(VRMIN[i] - VR);
    } else {
      fvalue[2 + 2 * i + 1] = PetscRealPart((VR - KA[i] * RF + KA[i] * KF[i] * Efd / TF[i] - KA[i] * (Vref[i] - Vm)) / TA[i]);
    }
    idx = idx + 9;
  }
  PetscCall(VecRestoreArrayRead(Xgen, &xgen));
  PetscCall(VecRestoreArrayRead(Xnet, &xnet));

  PetscCall(DMCompositeRestoreLocalVectors(user->dmpgrid, &Xgen, &Xnet));

  PetscFunctionReturn(PETSC_SUCCESS);
}

PetscErrorCode PostEventFunction(TS ts, PetscInt nevents, PetscInt event_list[], PetscReal t, Vec X, PetscBool forwardsolve, void *ctx)
{
  Userctx     *user = (Userctx *)ctx;
  Vec          Xgen, Xnet;
  PetscScalar *xgen, *xnet;
  PetscInt     row_loc, col_loc;
  PetscScalar  val;
  PetscInt     i, idx = 0, event_num;
  PetscScalar  fvalue;
  PetscScalar  Efd, RF, VR;
  PetscScalar  Vr, Vi, Vm;

  PetscFunctionBegin;

  PetscCall(DMCompositeGetLocalVectors(user->dmpgrid, &Xgen, &Xnet));
  PetscCall(DMCompositeScatter(user->dmpgrid, X, Xgen, Xnet));

  PetscCall(VecGetArray(Xgen, &xgen));
  PetscCall(VecGetArray(Xnet, &xnet));

  for (i = 0; i < nevents; i++) {
    if (event_list[i] == 0) {
      /* Apply disturbance - resistive fault at user->faultbus */
      /* This is done by adding shunt conductance to the diagonal location
         in the Ybus matrix */
      row_loc = 2 * user->faultbus;
      col_loc = 2 * user->faultbus + 1; /* Location for G */
      val     = 1 / user->Rfault;
      PetscCall(MatSetValues(user->Ybus, 1, &row_loc, 1, &col_loc, &val, ADD_VALUES));
      row_loc = 2 * user->faultbus + 1;
      col_loc = 2 * user->faultbus; /* Location for G */
      val     = 1 / user->Rfault;
      PetscCall(MatSetValues(user->Ybus, 1, &row_loc, 1, &col_loc, &val, ADD_VALUES));

      PetscCall(MatAssemblyBegin(user->Ybus, MAT_FINAL_ASSEMBLY));
      PetscCall(MatAssemblyEnd(user->Ybus, MAT_FINAL_ASSEMBLY));

      /* Solve the algebraic equations */
      PetscCall(SNESSolve(user->snes_alg, NULL, X));
    } else if (event_list[i] == 1) {
      /* Remove the fault */
      row_loc = 2 * user->faultbus;
      col_loc = 2 * user->faultbus + 1;
      val     = -1 / user->Rfault;
      PetscCall(MatSetValues(user->Ybus, 1, &row_loc, 1, &col_loc, &val, ADD_VALUES));
      row_loc = 2 * user->faultbus + 1;
      col_loc = 2 * user->faultbus;
      val     = -1 / user->Rfault;
      PetscCall(MatSetValues(user->Ybus, 1, &row_loc, 1, &col_loc, &val, ADD_VALUES));

      PetscCall(MatAssemblyBegin(user->Ybus, MAT_FINAL_ASSEMBLY));
      PetscCall(MatAssemblyEnd(user->Ybus, MAT_FINAL_ASSEMBLY));

      /* Solve the algebraic equations */
      PetscCall(SNESSolve(user->snes_alg, NULL, X));

      /* Check the VR derivatives and reset flags if needed */
      for (i = 0; i < ngen; i++) {
        Efd = xgen[idx + 6];
        RF  = xgen[idx + 7];
        VR  = xgen[idx + 8];

        Vr = xnet[2 * gbus[i]];     /* Real part of generator terminal voltage */
        Vi = xnet[2 * gbus[i] + 1]; /* Imaginary part of the generator terminal voltage */
        Vm = PetscSqrtScalar(Vr * Vr + Vi * Vi);

        if (VRatmax[i]) {
          fvalue = (VR - KA[i] * RF + KA[i] * KF[i] * Efd / TF[i] - KA[i] * (Vref[i] - Vm)) / TA[i];
          if (fvalue < 0) {
            VRatmax[i] = 0;
            PetscCall(PetscPrintf(PETSC_COMM_SELF, "VR[%" PetscInt_FMT "]: dVR_dt went negative on fault clearing at time %g\n", i, (double)t));
          }
        }
        if (VRatmin[i]) {
          fvalue = (VR - KA[i] * RF + KA[i] * KF[i] * Efd / TF[i] - KA[i] * (Vref[i] - Vm)) / TA[i];

          if (fvalue > 0) {
            VRatmin[i] = 0;
            PetscCall(PetscPrintf(PETSC_COMM_SELF, "VR[%" PetscInt_FMT "]: dVR_dt went positive on fault clearing at time %g\n", i, (double)t));
          }
        }
        idx = idx + 9;
      }
    } else {
      idx       = (event_list[i] - 2) / 2;
      event_num = (event_list[i] - 2) % 2;
      if (event_num == 0) { /* Max VR */
        if (!VRatmax[idx]) {
          VRatmax[idx] = 1;
          PetscCall(PetscPrintf(PETSC_COMM_SELF, "VR[%" PetscInt_FMT "]: hit upper limit at time %g\n", idx, (double)t));
        } else {
          VRatmax[idx] = 0;
          PetscCall(PetscPrintf(PETSC_COMM_SELF, "VR[%" PetscInt_FMT "]: freeing variable as dVR_dt is negative at time %g\n", idx, (double)t));
        }
      } else {
        if (!VRatmin[idx]) {
          VRatmin[idx] = 1;
          PetscCall(PetscPrintf(PETSC_COMM_SELF, "VR[%" PetscInt_FMT "]: hit lower limit at time %g\n", idx, (double)t));
        } else {
          VRatmin[idx] = 0;
          PetscCall(PetscPrintf(PETSC_COMM_SELF, "VR[%" PetscInt_FMT "]: freeing variable as dVR_dt is positive at time %g\n", idx, (double)t));
        }
      }
    }
  }

  PetscCall(VecRestoreArray(Xgen, &xgen));
  PetscCall(VecRestoreArray(Xnet, &xnet));

  PetscCall(DMCompositeRestoreLocalVectors(user->dmpgrid, &Xgen, &Xnet));

  PetscFunctionReturn(PETSC_SUCCESS);
}

/* Converts from machine frame (dq) to network (phase a real,imag) reference frame */
PetscErrorCode dq2ri(PetscScalar Fd, PetscScalar Fq, PetscScalar delta, PetscScalar *Fr, PetscScalar *Fi)
{
  PetscFunctionBegin;
  *Fr = Fd * PetscSinScalar(delta) + Fq * PetscCosScalar(delta);
  *Fi = -Fd * PetscCosScalar(delta) + Fq * PetscSinScalar(delta);
  PetscFunctionReturn(PETSC_SUCCESS);
}

/* Converts from network frame ([phase a real,imag) to machine (dq) reference frame */
PetscErrorCode ri2dq(PetscScalar Fr, PetscScalar Fi, PetscScalar delta, PetscScalar *Fd, PetscScalar *Fq)
{
  PetscFunctionBegin;
  *Fd = Fr * PetscSinScalar(delta) - Fi * PetscCosScalar(delta);
  *Fq = Fr * PetscCosScalar(delta) + Fi * PetscSinScalar(delta);
  PetscFunctionReturn(PETSC_SUCCESS);
}

/* Saves the solution at each time to a matrix */
PetscErrorCode SaveSolution(TS ts)
{
  Userctx           *user;
  Vec                X;
  const PetscScalar *x;
  PetscScalar       *mat;
  PetscInt           idx;
  PetscReal          t;

  PetscFunctionBegin;
  PetscCall(TSGetApplicationContext(ts, &user));
  PetscCall(TSGetTime(ts, &t));
  PetscCall(TSGetSolution(ts, &X));
  idx = user->stepnum * (user->neqs_pgrid + 1);
  PetscCall(MatDenseGetArray(user->Sol, &mat));
  PetscCall(VecGetArrayRead(X, &x));
  mat[idx] = t;
  PetscCall(PetscArraycpy(mat + idx + 1, x, user->neqs_pgrid));
  PetscCall(MatDenseRestoreArray(user->Sol, &mat));
  PetscCall(VecRestoreArrayRead(X, &x));
  user->stepnum++;
  PetscFunctionReturn(PETSC_SUCCESS);
}

PetscErrorCode SetInitialGuess(Vec X, Userctx *user)
{
  Vec                Xgen, Xnet;
  PetscScalar       *xgen;
  const PetscScalar *xnet;
  PetscInt           i, idx = 0;
  PetscScalar        Vr, Vi, IGr, IGi, Vm, Vm2;
  PetscScalar        Eqp, Edp, delta;
  PetscScalar        Efd, RF, VR; /* Exciter variables */
  PetscScalar        Id, Iq;      /* Generator dq axis currents */
  PetscScalar        theta, Vd, Vq, SE;

  PetscFunctionBegin;
  M[0] = 2 * H[0] / w_s;
  M[1] = 2 * H[1] / w_s;
  M[2] = 2 * H[2] / w_s;
  D[0] = 0.1 * M[0];
  D[1] = 0.1 * M[1];
  D[2] = 0.1 * M[2];

  PetscCall(DMCompositeGetLocalVectors(user->dmpgrid, &Xgen, &Xnet));

  /* Network subsystem initialization */
  PetscCall(VecCopy(user->V0, Xnet));

  /* Generator subsystem initialization */
  PetscCall(VecGetArrayWrite(Xgen, &xgen));
  PetscCall(VecGetArrayRead(Xnet, &xnet));

  for (i = 0; i < ngen; i++) {
    Vr  = xnet[2 * gbus[i]];     /* Real part of generator terminal voltage */
    Vi  = xnet[2 * gbus[i] + 1]; /* Imaginary part of the generator terminal voltage */
    Vm  = PetscSqrtScalar(Vr * Vr + Vi * Vi);
    Vm2 = Vm * Vm;
    IGr = (Vr * PG[i] + Vi * QG[i]) / Vm2;
    IGi = (Vi * PG[i] - Vr * QG[i]) / Vm2;

    delta = PetscAtan2Real(Vi + Xq[i] * IGr, Vr - Xq[i] * IGi); /* Machine angle */

    theta = PETSC_PI / 2.0 - delta;

    Id = IGr * PetscCosScalar(theta) - IGi * PetscSinScalar(theta); /* d-axis stator current */
    Iq = IGr * PetscSinScalar(theta) + IGi * PetscCosScalar(theta); /* q-axis stator current */

    Vd = Vr * PetscCosScalar(theta) - Vi * PetscSinScalar(theta);
    Vq = Vr * PetscSinScalar(theta) + Vi * PetscCosScalar(theta);

    Edp = Vd + Rs[i] * Id - Xqp[i] * Iq; /* d-axis transient EMF */
    Eqp = Vq + Rs[i] * Iq + Xdp[i] * Id; /* q-axis transient EMF */

    TM[i] = PG[i];

    /* The generator variables are ordered as [Eqp,Edp,delta,w,Id,Iq] */
    xgen[idx]     = Eqp;
    xgen[idx + 1] = Edp;
    xgen[idx + 2] = delta;
    xgen[idx + 3] = w_s;

    idx = idx + 4;

    xgen[idx]     = Id;
    xgen[idx + 1] = Iq;

    idx = idx + 2;

    /* Exciter */
    Efd = Eqp + (Xd[i] - Xdp[i]) * Id;
    SE  = k1[i] * PetscExpScalar(k2[i] * Efd);
    VR  = KE[i] * Efd + SE;
    RF  = KF[i] * Efd / TF[i];

    xgen[idx]     = Efd;
    xgen[idx + 1] = RF;
    xgen[idx + 2] = VR;

    Vref[i] = Vm + (VR / KA[i]);

    VRatmin[i] = VRatmax[i] = 0;

    idx = idx + 3;
  }
  PetscCall(VecRestoreArrayWrite(Xgen, &xgen));
  PetscCall(VecRestoreArrayRead(Xnet, &xnet));

  /* PetscCall(VecView(Xgen,0)); */
  PetscCall(DMCompositeGather(user->dmpgrid, INSERT_VALUES, X, Xgen, Xnet));
  PetscCall(DMCompositeRestoreLocalVectors(user->dmpgrid, &Xgen, &Xnet));
  PetscFunctionReturn(PETSC_SUCCESS);
}

/* Computes F = [f(x,y);g(x,y)] */
PetscErrorCode ResidualFunction(Vec X, Vec F, Userctx *user)
{
  Vec                Xgen, Xnet, Fgen, Fnet;
  const PetscScalar *xgen, *xnet;
  PetscScalar       *fgen, *fnet;
  PetscInt           i, idx = 0;
  PetscScalar        Vr, Vi, Vm, Vm2;
  PetscScalar        Eqp, Edp, delta, w; /* Generator variables */
  PetscScalar        Efd, RF, VR;        /* Exciter variables */
  PetscScalar        Id, Iq;             /* Generator dq axis currents */
  PetscScalar        Vd, Vq, SE;
  PetscScalar        IGr, IGi, IDr, IDi;
  PetscScalar        Zdq_inv[4], det;
  PetscScalar        PD, QD, Vm0, *v0;
  PetscInt           k;

  PetscFunctionBegin;
  PetscCall(VecZeroEntries(F));
  PetscCall(DMCompositeGetLocalVectors(user->dmpgrid, &Xgen, &Xnet));
  PetscCall(DMCompositeGetLocalVectors(user->dmpgrid, &Fgen, &Fnet));
  PetscCall(DMCompositeScatter(user->dmpgrid, X, Xgen, Xnet));
  PetscCall(DMCompositeScatter(user->dmpgrid, F, Fgen, Fnet));

  /* Network current balance residual IG + Y*V + IL = 0. Only YV is added here.
     The generator current injection, IG, and load current injection, ID are added later
  */
  /* Note that the values in Ybus are stored assuming the imaginary current balance
     equation is ordered first followed by real current balance equation for each bus.
     Thus imaginary current contribution goes in location 2*i, and
     real current contribution in 2*i+1
  */
  PetscCall(MatMult(user->Ybus, Xnet, Fnet));

  PetscCall(VecGetArrayRead(Xgen, &xgen));
  PetscCall(VecGetArrayRead(Xnet, &xnet));
  PetscCall(VecGetArrayWrite(Fgen, &fgen));
  PetscCall(VecGetArrayWrite(Fnet, &fnet));

  /* Generator subsystem */
  for (i = 0; i < ngen; i++) {
    Eqp   = xgen[idx];
    Edp   = xgen[idx + 1];
    delta = xgen[idx + 2];
    w     = xgen[idx + 3];
    Id    = xgen[idx + 4];
    Iq    = xgen[idx + 5];
    Efd   = xgen[idx + 6];
    RF    = xgen[idx + 7];
    VR    = xgen[idx + 8];

    /* Generator differential equations */
    fgen[idx]     = (-Eqp - (Xd[i] - Xdp[i]) * Id + Efd) / Td0p[i];
    fgen[idx + 1] = (-Edp + (Xq[i] - Xqp[i]) * Iq) / Tq0p[i];
    fgen[idx + 2] = w - w_s;
    fgen[idx + 3] = (TM[i] - Edp * Id - Eqp * Iq - (Xqp[i] - Xdp[i]) * Id * Iq - D[i] * (w - w_s)) / M[i];

    Vr = xnet[2 * gbus[i]];     /* Real part of generator terminal voltage */
    Vi = xnet[2 * gbus[i] + 1]; /* Imaginary part of the generator terminal voltage */

    PetscCall(ri2dq(Vr, Vi, delta, &Vd, &Vq));
    /* Algebraic equations for stator currents */
    det = Rs[i] * Rs[i] + Xdp[i] * Xqp[i];

    Zdq_inv[0] = Rs[i] / det;
    Zdq_inv[1] = Xqp[i] / det;
    Zdq_inv[2] = -Xdp[i] / det;
    Zdq_inv[3] = Rs[i] / det;

    fgen[idx + 4] = Zdq_inv[0] * (-Edp + Vd) + Zdq_inv[1] * (-Eqp + Vq) + Id;
    fgen[idx + 5] = Zdq_inv[2] * (-Edp + Vd) + Zdq_inv[3] * (-Eqp + Vq) + Iq;

    /* Add generator current injection to network */
    PetscCall(dq2ri(Id, Iq, delta, &IGr, &IGi));

    fnet[2 * gbus[i]] -= IGi;
    fnet[2 * gbus[i] + 1] -= IGr;

    Vm = PetscSqrtScalar(Vd * Vd + Vq * Vq);

    SE = k1[i] * PetscExpScalar(k2[i] * Efd);

    /* Exciter differential equations */
    fgen[idx + 6] = (-KE[i] * Efd - SE + VR) / TE[i];
    fgen[idx + 7] = (-RF + KF[i] * Efd / TF[i]) / TF[i];
    if (VRatmax[i]) fgen[idx + 8] = VR - VRMAX[i];
    else if (VRatmin[i]) fgen[idx + 8] = VRMIN[i] - VR;
    else fgen[idx + 8] = (-VR + KA[i] * RF - KA[i] * KF[i] * Efd / TF[i] + KA[i] * (Vref[i] - Vm)) / TA[i];

    idx = idx + 9;
  }

  PetscCall(VecGetArray(user->V0, &v0));
  for (i = 0; i < nload; i++) {
    Vr  = xnet[2 * lbus[i]];     /* Real part of load bus voltage */
    Vi  = xnet[2 * lbus[i] + 1]; /* Imaginary part of the load bus voltage */
    Vm  = PetscSqrtScalar(Vr * Vr + Vi * Vi);
    Vm2 = Vm * Vm;
    Vm0 = PetscSqrtScalar(v0[2 * lbus[i]] * v0[2 * lbus[i]] + v0[2 * lbus[i] + 1] * v0[2 * lbus[i] + 1]);
    PD = QD = 0.0;
    for (k = 0; k < ld_nsegsp[i]; k++) PD += ld_alphap[k] * PD0[i] * PetscPowScalar((Vm / Vm0), ld_betap[k]);
    for (k = 0; k < ld_nsegsq[i]; k++) QD += ld_alphaq[k] * QD0[i] * PetscPowScalar((Vm / Vm0), ld_betaq[k]);

    /* Load currents */
    IDr = (PD * Vr + QD * Vi) / Vm2;
    IDi = (-QD * Vr + PD * Vi) / Vm2;

    fnet[2 * lbus[i]] += IDi;
    fnet[2 * lbus[i] + 1] += IDr;
  }
  PetscCall(VecRestoreArray(user->V0, &v0));

  PetscCall(VecRestoreArrayRead(Xgen, &xgen));
  PetscCall(VecRestoreArrayRead(Xnet, &xnet));
  PetscCall(VecRestoreArrayWrite(Fgen, &fgen));
  PetscCall(VecRestoreArrayWrite(Fnet, &fnet));

  PetscCall(DMCompositeGather(user->dmpgrid, INSERT_VALUES, F, Fgen, Fnet));
  PetscCall(DMCompositeRestoreLocalVectors(user->dmpgrid, &Xgen, &Xnet));
  PetscCall(DMCompositeRestoreLocalVectors(user->dmpgrid, &Fgen, &Fnet));
  PetscFunctionReturn(PETSC_SUCCESS);
}

/*   f(x,y)
     g(x,y)
 */
PetscErrorCode RHSFunction(TS ts, PetscReal t, Vec X, Vec F, void *ctx)
{
  Userctx *user = (Userctx *)ctx;

  PetscFunctionBegin;
  user->t = t;
  PetscCall(ResidualFunction(X, F, user));
  PetscFunctionReturn(PETSC_SUCCESS);
}

/* f(x,y) - \dot{x}
     g(x,y) = 0
 */
PetscErrorCode IFunction(TS ts, PetscReal t, Vec X, Vec Xdot, Vec F, void *ctx)
{
  PetscScalar       *f;
  const PetscScalar *xdot;
  PetscInt           i;

  PetscFunctionBegin;
  PetscCall(RHSFunction(ts, t, X, F, ctx));
  PetscCall(VecScale(F, -1.0));
  PetscCall(VecGetArray(F, &f));
  PetscCall(VecGetArrayRead(Xdot, &xdot));
  for (i = 0; i < ngen; i++) {
    f[9 * i] += xdot[9 * i];
    f[9 * i + 1] += xdot[9 * i + 1];
    f[9 * i + 2] += xdot[9 * i + 2];
    f[9 * i + 3] += xdot[9 * i + 3];
    f[9 * i + 6] += xdot[9 * i + 6];
    f[9 * i + 7] += xdot[9 * i + 7];
    f[9 * i + 8] += xdot[9 * i + 8];
  }
  PetscCall(VecRestoreArray(F, &f));
  PetscCall(VecRestoreArrayRead(Xdot, &xdot));
  PetscFunctionReturn(PETSC_SUCCESS);
}

/* This function is used for solving the algebraic system only during fault on and
   off times. It computes the entire F and then zeros out the part corresponding to
   differential equations
 F = [0;g(y)];
*/
PetscErrorCode AlgFunction(SNES snes, Vec X, Vec F, void *ctx)
{
  Userctx     *user = (Userctx *)ctx;
  PetscScalar *f;
  PetscInt     i;

  PetscFunctionBegin;
  PetscCall(ResidualFunction(X, F, user));
  PetscCall(VecGetArray(F, &f));
  for (i = 0; i < ngen; i++) {
    f[9 * i]     = 0;
    f[9 * i + 1] = 0;
    f[9 * i + 2] = 0;
    f[9 * i + 3] = 0;
    f[9 * i + 6] = 0;
    f[9 * i + 7] = 0;
    f[9 * i + 8] = 0;
  }
  PetscCall(VecRestoreArray(F, &f));
  PetscFunctionReturn(PETSC_SUCCESS);
}

PetscErrorCode PostStage(TS ts, PetscReal t, PetscInt i, Vec *X)
{
  Userctx *user;

  PetscFunctionBegin;
  PetscCall(TSGetApplicationContext(ts, &user));
  PetscCall(SNESSolve(user->snes_alg, NULL, X[i]));
  PetscFunctionReturn(PETSC_SUCCESS);
}

PetscErrorCode PostEvaluate(TS ts)
{
  Userctx *user;
  Vec      X;

  PetscFunctionBegin;
  PetscCall(TSGetApplicationContext(ts, &user));
  PetscCall(TSGetSolution(ts, &X));
  PetscCall(SNESSolve(user->snes_alg, NULL, X));
  PetscFunctionReturn(PETSC_SUCCESS);
}

PetscErrorCode PreallocateJacobian(Mat J, Userctx *user)
{
  PetscInt *d_nnz;
  PetscInt  i, idx = 0, start = 0;
  PetscInt  ncols;

  PetscFunctionBegin;
  PetscCall(PetscMalloc1(user->neqs_pgrid, &d_nnz));
  for (i = 0; i < user->neqs_pgrid; i++) d_nnz[i] = 0;
  /* Generator subsystem */
  for (i = 0; i < ngen; i++) {
    d_nnz[idx] += 3;
    d_nnz[idx + 1] += 2;
    d_nnz[idx + 2] += 2;
    d_nnz[idx + 3] += 5;
    d_nnz[idx + 4] += 6;
    d_nnz[idx + 5] += 6;

    d_nnz[user->neqs_gen + 2 * gbus[i]] += 3;
    d_nnz[user->neqs_gen + 2 * gbus[i] + 1] += 3;

    d_nnz[idx + 6] += 2;
    d_nnz[idx + 7] += 2;
    d_nnz[idx + 8] += 5;

    idx = idx + 9;
  }
  start = user->neqs_gen;

  for (i = 0; i < nbus; i++) {
    PetscCall(MatGetRow(user->Ybus, 2 * i, &ncols, NULL, NULL));
    d_nnz[start + 2 * i] += ncols;
    d_nnz[start + 2 * i + 1] += ncols;
    PetscCall(MatRestoreRow(user->Ybus, 2 * i, &ncols, NULL, NULL));
  }
  PetscCall(MatSeqAIJSetPreallocation(J, 0, d_nnz));
  PetscCall(PetscFree(d_nnz));
  PetscFunctionReturn(PETSC_SUCCESS);
}

/*
   J = [df_dx, df_dy
        dg_dx, dg_dy]
*/
PetscErrorCode ResidualJacobian(Vec X, Mat J, Mat B, void *ctx)
{
  Userctx           *user = (Userctx *)ctx;
  Vec                Xgen, Xnet;
  const PetscScalar *xgen, *xnet;
  PetscInt           i, idx = 0;
  PetscScalar        Vr, Vi, Vm, Vm2;
  PetscScalar        Eqp, Edp, delta; /* Generator variables */
  PetscScalar        Efd;
  PetscScalar        Id, Iq; /* Generator dq axis currents */
  PetscScalar        Vd, Vq;
  PetscScalar        val[10];
  PetscInt           row[2], col[10];
  PetscInt           net_start = user->neqs_gen;
  PetscScalar        Zdq_inv[4], det;
  PetscScalar        dVd_dVr, dVd_dVi, dVq_dVr, dVq_dVi, dVd_ddelta, dVq_ddelta;
  PetscScalar        dIGr_ddelta, dIGi_ddelta, dIGr_dId, dIGr_dIq, dIGi_dId, dIGi_dIq;
  PetscScalar        dSE_dEfd;
  PetscScalar        dVm_dVd, dVm_dVq, dVm_dVr, dVm_dVi;
  PetscInt           ncols;
  const PetscInt    *cols;
  const PetscScalar *yvals;
  PetscInt           k;
  PetscScalar        PD, QD, Vm0, Vm4;
  const PetscScalar *v0;
  PetscScalar        dPD_dVr, dPD_dVi, dQD_dVr, dQD_dVi;
  PetscScalar        dIDr_dVr, dIDr_dVi, dIDi_dVr, dIDi_dVi;

  PetscFunctionBegin;
  PetscCall(MatZeroEntries(B));
  PetscCall(DMCompositeGetLocalVectors(user->dmpgrid, &Xgen, &Xnet));
  PetscCall(DMCompositeScatter(user->dmpgrid, X, Xgen, Xnet));

  PetscCall(VecGetArrayRead(Xgen, &xgen));
  PetscCall(VecGetArrayRead(Xnet, &xnet));

  /* Generator subsystem */
  for (i = 0; i < ngen; i++) {
    Eqp   = xgen[idx];
    Edp   = xgen[idx + 1];
    delta = xgen[idx + 2];
    Id    = xgen[idx + 4];
    Iq    = xgen[idx + 5];
    Efd   = xgen[idx + 6];

    /*    fgen[idx]   = (-Eqp - (Xd[i] - Xdp[i])*Id + Efd)/Td0p[i]; */
    row[0] = idx;
    col[0] = idx;
    col[1] = idx + 4;
    col[2] = idx + 6;
    val[0] = -1 / Td0p[i];
    val[1] = -(Xd[i] - Xdp[i]) / Td0p[i];
    val[2] = 1 / Td0p[i];

    PetscCall(MatSetValues(J, 1, row, 3, col, val, INSERT_VALUES));

    /*    fgen[idx+1] = (-Edp + (Xq[i] - Xqp[i])*Iq)/Tq0p[i]; */
    row[0] = idx + 1;
    col[0] = idx + 1;
    col[1] = idx + 5;
    val[0] = -1 / Tq0p[i];
    val[1] = (Xq[i] - Xqp[i]) / Tq0p[i];
    PetscCall(MatSetValues(J, 1, row, 2, col, val, INSERT_VALUES));

    /*    fgen[idx+2] = w - w_s; */
    row[0] = idx + 2;
    col[0] = idx + 2;
    col[1] = idx + 3;
    val[0] = 0;
    val[1] = 1;
    PetscCall(MatSetValues(J, 1, row, 2, col, val, INSERT_VALUES));

    /*    fgen[idx+3] = (TM[i] - Edp*Id - Eqp*Iq - (Xqp[i] - Xdp[i])*Id*Iq - D[i]*(w - w_s))/M[i]; */
    row[0] = idx + 3;
    col[0] = idx;
    col[1] = idx + 1;
    col[2] = idx + 3;
    col[3] = idx + 4;
    col[4] = idx + 5;
    val[0] = -Iq / M[i];
    val[1] = -Id / M[i];
    val[2] = -D[i] / M[i];
    val[3] = (-Edp - (Xqp[i] - Xdp[i]) * Iq) / M[i];
    val[4] = (-Eqp - (Xqp[i] - Xdp[i]) * Id) / M[i];
    PetscCall(MatSetValues(J, 1, row, 5, col, val, INSERT_VALUES));

    Vr = xnet[2 * gbus[i]];     /* Real part of generator terminal voltage */
    Vi = xnet[2 * gbus[i] + 1]; /* Imaginary part of the generator terminal voltage */
    PetscCall(ri2dq(Vr, Vi, delta, &Vd, &Vq));

    det = Rs[i] * Rs[i] + Xdp[i] * Xqp[i];

    Zdq_inv[0] = Rs[i] / det;
    Zdq_inv[1] = Xqp[i] / det;
    Zdq_inv[2] = -Xdp[i] / det;
    Zdq_inv[3] = Rs[i] / det;

    dVd_dVr    = PetscSinScalar(delta);
    dVd_dVi    = -PetscCosScalar(delta);
    dVq_dVr    = PetscCosScalar(delta);
    dVq_dVi    = PetscSinScalar(delta);
    dVd_ddelta = Vr * PetscCosScalar(delta) + Vi * PetscSinScalar(delta);
    dVq_ddelta = -Vr * PetscSinScalar(delta) + Vi * PetscCosScalar(delta);

    /*    fgen[idx+4] = Zdq_inv[0]*(-Edp + Vd) + Zdq_inv[1]*(-Eqp + Vq) + Id; */
    row[0] = idx + 4;
    col[0] = idx;
    col[1] = idx + 1;
    col[2] = idx + 2;
    val[0] = -Zdq_inv[1];
    val[1] = -Zdq_inv[0];
    val[2] = Zdq_inv[0] * dVd_ddelta + Zdq_inv[1] * dVq_ddelta;
    col[3] = idx + 4;
    col[4] = net_start + 2 * gbus[i];
    col[5] = net_start + 2 * gbus[i] + 1;
    val[3] = 1;
    val[4] = Zdq_inv[0] * dVd_dVr + Zdq_inv[1] * dVq_dVr;
    val[5] = Zdq_inv[0] * dVd_dVi + Zdq_inv[1] * dVq_dVi;
    PetscCall(MatSetValues(J, 1, row, 6, col, val, INSERT_VALUES));

    /*  fgen[idx+5] = Zdq_inv[2]*(-Edp + Vd) + Zdq_inv[3]*(-Eqp + Vq) + Iq; */
    row[0] = idx + 5;
    col[0] = idx;
    col[1] = idx + 1;
    col[2] = idx + 2;
    val[0] = -Zdq_inv[3];
    val[1] = -Zdq_inv[2];
    val[2] = Zdq_inv[2] * dVd_ddelta + Zdq_inv[3] * dVq_ddelta;
    col[3] = idx + 5;
    col[4] = net_start + 2 * gbus[i];
    col[5] = net_start + 2 * gbus[i] + 1;
    val[3] = 1;
    val[4] = Zdq_inv[2] * dVd_dVr + Zdq_inv[3] * dVq_dVr;
    val[5] = Zdq_inv[2] * dVd_dVi + Zdq_inv[3] * dVq_dVi;
    PetscCall(MatSetValues(J, 1, row, 6, col, val, INSERT_VALUES));

    dIGr_ddelta = Id * PetscCosScalar(delta) - Iq * PetscSinScalar(delta);
    dIGi_ddelta = Id * PetscSinScalar(delta) + Iq * PetscCosScalar(delta);
    dIGr_dId    = PetscSinScalar(delta);
    dIGr_dIq    = PetscCosScalar(delta);
    dIGi_dId    = -PetscCosScalar(delta);
    dIGi_dIq    = PetscSinScalar(delta);

    /* fnet[2*gbus[i]]   -= IGi; */
    row[0] = net_start + 2 * gbus[i];
    col[0] = idx + 2;
    col[1] = idx + 4;
    col[2] = idx + 5;
    val[0] = -dIGi_ddelta;
    val[1] = -dIGi_dId;
    val[2] = -dIGi_dIq;
    PetscCall(MatSetValues(J, 1, row, 3, col, val, INSERT_VALUES));

    /* fnet[2*gbus[i]+1]   -= IGr; */
    row[0] = net_start + 2 * gbus[i] + 1;
    col[0] = idx + 2;
    col[1] = idx + 4;
    col[2] = idx + 5;
    val[0] = -dIGr_ddelta;
    val[1] = -dIGr_dId;
    val[2] = -dIGr_dIq;
    PetscCall(MatSetValues(J, 1, row, 3, col, val, INSERT_VALUES));

    Vm = PetscSqrtScalar(Vd * Vd + Vq * Vq);

    /*    fgen[idx+6] = (-KE[i]*Efd - SE + VR)/TE[i]; */
    /*    SE  = k1[i]*PetscExpScalar(k2[i]*Efd); */

    dSE_dEfd = k1[i] * k2[i] * PetscExpScalar(k2[i] * Efd);

    row[0] = idx + 6;
    col[0] = idx + 6;
    col[1] = idx + 8;
    val[0] = (-KE[i] - dSE_dEfd) / TE[i];
    val[1] = 1 / TE[i];
    PetscCall(MatSetValues(J, 1, row, 2, col, val, INSERT_VALUES));

    /* Exciter differential equations */

    /*    fgen[idx+7] = (-RF + KF[i]*Efd/TF[i])/TF[i]; */
    row[0] = idx + 7;
    col[0] = idx + 6;
    col[1] = idx + 7;
    val[0] = (KF[i] / TF[i]) / TF[i];
    val[1] = -1 / TF[i];
    PetscCall(MatSetValues(J, 1, row, 2, col, val, INSERT_VALUES));

    /*    fgen[idx+8] = (-VR + KA[i]*RF - KA[i]*KF[i]*Efd/TF[i] + KA[i]*(Vref[i] - Vm))/TA[i]; */
    /* Vm = (Vd^2 + Vq^2)^0.5; */

    row[0] = idx + 8;
    if (VRatmax[i]) {
      col[0] = idx + 8;
      val[0] = 1.0;
      PetscCall(MatSetValues(J, 1, row, 1, col, val, INSERT_VALUES));
    } else if (VRatmin[i]) {
      col[0] = idx + 8;
      val[0] = -1.0;
      PetscCall(MatSetValues(J, 1, row, 1, col, val, INSERT_VALUES));
    } else {
      dVm_dVd = Vd / Vm;
      dVm_dVq = Vq / Vm;
      dVm_dVr = dVm_dVd * dVd_dVr + dVm_dVq * dVq_dVr;
      dVm_dVi = dVm_dVd * dVd_dVi + dVm_dVq * dVq_dVi;
      row[0]  = idx + 8;
      col[0]  = idx + 6;
      col[1]  = idx + 7;
      col[2]  = idx + 8;
      val[0]  = -(KA[i] * KF[i] / TF[i]) / TA[i];
      val[1]  = KA[i] / TA[i];
      val[2]  = -1 / TA[i];
      col[3]  = net_start + 2 * gbus[i];
      col[4]  = net_start + 2 * gbus[i] + 1;
      val[3]  = -KA[i] * dVm_dVr / TA[i];
      val[4]  = -KA[i] * dVm_dVi / TA[i];
      PetscCall(MatSetValues(J, 1, row, 5, col, val, INSERT_VALUES));
    }
    idx = idx + 9;
  }

  for (i = 0; i < nbus; i++) {
    PetscCall(MatGetRow(user->Ybus, 2 * i, &ncols, &cols, &yvals));
    row[0] = net_start + 2 * i;
    for (k = 0; k < ncols; k++) {
      col[k] = net_start + cols[k];
      val[k] = yvals[k];
    }
    PetscCall(MatSetValues(J, 1, row, ncols, col, val, INSERT_VALUES));
    PetscCall(MatRestoreRow(user->Ybus, 2 * i, &ncols, &cols, &yvals));

    PetscCall(MatGetRow(user->Ybus, 2 * i + 1, &ncols, &cols, &yvals));
    row[0] = net_start + 2 * i + 1;
    for (k = 0; k < ncols; k++) {
      col[k] = net_start + cols[k];
      val[k] = yvals[k];
    }
    PetscCall(MatSetValues(J, 1, row, ncols, col, val, INSERT_VALUES));
    PetscCall(MatRestoreRow(user->Ybus, 2 * i + 1, &ncols, &cols, &yvals));
  }

  PetscCall(MatAssemblyBegin(J, MAT_FLUSH_ASSEMBLY));
  PetscCall(MatAssemblyEnd(J, MAT_FLUSH_ASSEMBLY));

  PetscCall(VecGetArrayRead(user->V0, &v0));
  for (i = 0; i < nload; i++) {
    Vr  = xnet[2 * lbus[i]];     /* Real part of load bus voltage */
    Vi  = xnet[2 * lbus[i] + 1]; /* Imaginary part of the load bus voltage */
    Vm  = PetscSqrtScalar(Vr * Vr + Vi * Vi);
    Vm2 = Vm * Vm;
    Vm4 = Vm2 * Vm2;
    Vm0 = PetscSqrtScalar(v0[2 * lbus[i]] * v0[2 * lbus[i]] + v0[2 * lbus[i] + 1] * v0[2 * lbus[i] + 1]);
    PD = QD = 0.0;
    dPD_dVr = dPD_dVi = dQD_dVr = dQD_dVi = 0.0;
    for (k = 0; k < ld_nsegsp[i]; k++) {
      PD += ld_alphap[k] * PD0[i] * PetscPowScalar((Vm / Vm0), ld_betap[k]);
      dPD_dVr += ld_alphap[k] * ld_betap[k] * PD0[i] * PetscPowScalar((1 / Vm0), ld_betap[k]) * Vr * PetscPowScalar(Vm, (ld_betap[k] - 2));
      dPD_dVi += ld_alphap[k] * ld_betap[k] * PD0[i] * PetscPowScalar((1 / Vm0), ld_betap[k]) * Vi * PetscPowScalar(Vm, (ld_betap[k] - 2));
    }
    for (k = 0; k < ld_nsegsq[i]; k++) {
      QD += ld_alphaq[k] * QD0[i] * PetscPowScalar((Vm / Vm0), ld_betaq[k]);
      dQD_dVr += ld_alphaq[k] * ld_betaq[k] * QD0[i] * PetscPowScalar((1 / Vm0), ld_betaq[k]) * Vr * PetscPowScalar(Vm, (ld_betaq[k] - 2));
      dQD_dVi += ld_alphaq[k] * ld_betaq[k] * QD0[i] * PetscPowScalar((1 / Vm0), ld_betaq[k]) * Vi * PetscPowScalar(Vm, (ld_betaq[k] - 2));
    }

    /*    IDr = (PD*Vr + QD*Vi)/Vm2; */
    /*    IDi = (-QD*Vr + PD*Vi)/Vm2; */

    dIDr_dVr = (dPD_dVr * Vr + dQD_dVr * Vi + PD) / Vm2 - ((PD * Vr + QD * Vi) * 2 * Vr) / Vm4;
    dIDr_dVi = (dPD_dVi * Vr + dQD_dVi * Vi + QD) / Vm2 - ((PD * Vr + QD * Vi) * 2 * Vi) / Vm4;

    dIDi_dVr = (-dQD_dVr * Vr + dPD_dVr * Vi - QD) / Vm2 - ((-QD * Vr + PD * Vi) * 2 * Vr) / Vm4;
    dIDi_dVi = (-dQD_dVi * Vr + dPD_dVi * Vi + PD) / Vm2 - ((-QD * Vr + PD * Vi) * 2 * Vi) / Vm4;

    /*    fnet[2*lbus[i]]   += IDi; */
    row[0] = net_start + 2 * lbus[i];
    col[0] = net_start + 2 * lbus[i];
    col[1] = net_start + 2 * lbus[i] + 1;
    val[0] = dIDi_dVr;
    val[1] = dIDi_dVi;
    PetscCall(MatSetValues(J, 1, row, 2, col, val, ADD_VALUES));
    /*    fnet[2*lbus[i]+1] += IDr; */
    row[0] = net_start + 2 * lbus[i] + 1;
    col[0] = net_start + 2 * lbus[i];
    col[1] = net_start + 2 * lbus[i] + 1;
    val[0] = dIDr_dVr;
    val[1] = dIDr_dVi;
    PetscCall(MatSetValues(J, 1, row, 2, col, val, ADD_VALUES));
  }
  PetscCall(VecRestoreArrayRead(user->V0, &v0));

  PetscCall(VecRestoreArrayRead(Xgen, &xgen));
  PetscCall(VecRestoreArrayRead(Xnet, &xnet));

  PetscCall(DMCompositeRestoreLocalVectors(user->dmpgrid, &Xgen, &Xnet));

  PetscCall(MatAssemblyBegin(J, MAT_FINAL_ASSEMBLY));
  PetscCall(MatAssemblyEnd(J, MAT_FINAL_ASSEMBLY));
  PetscFunctionReturn(PETSC_SUCCESS);
}

/*
   J = [I, 0
        dg_dx, dg_dy]
*/
PetscErrorCode AlgJacobian(SNES snes, Vec X, Mat A, Mat B, void *ctx)
{
  Userctx *user = (Userctx *)ctx;

  PetscFunctionBegin;
  PetscCall(ResidualJacobian(X, A, B, ctx));
  PetscCall(MatSetOption(A, MAT_KEEP_NONZERO_PATTERN, PETSC_TRUE));
  PetscCall(MatZeroRowsIS(A, user->is_diff, 1.0, NULL, NULL));
  PetscFunctionReturn(PETSC_SUCCESS);
}

/*
   J = [-df_dx, -df_dy
        dg_dx, dg_dy]
*/

PetscErrorCode RHSJacobian(TS ts, PetscReal t, Vec X, Mat A, Mat B, void *ctx)
{
  Userctx *user = (Userctx *)ctx;

  PetscFunctionBegin;
  user->t = t;

  PetscCall(ResidualJacobian(X, A, B, user));

  PetscFunctionReturn(PETSC_SUCCESS);
}

/*
   J = [df_dx-aI, df_dy
        dg_dx, dg_dy]
*/

PetscErrorCode IJacobian(TS ts, PetscReal t, Vec X, Vec Xdot, PetscReal a, Mat A, Mat B, Userctx *user)
{
  PetscScalar atmp = (PetscScalar)a;
  PetscInt    i, row;

  PetscFunctionBegin;
  user->t = t;

  PetscCall(RHSJacobian(ts, t, X, A, B, user));
  PetscCall(MatScale(B, -1.0));
  for (i = 0; i < ngen; i++) {
    row = 9 * i;
    PetscCall(MatSetValues(A, 1, &row, 1, &row, &atmp, ADD_VALUES));
    row = 9 * i + 1;
    PetscCall(MatSetValues(A, 1, &row, 1, &row, &atmp, ADD_VALUES));
    row = 9 * i + 2;
    PetscCall(MatSetValues(A, 1, &row, 1, &row, &atmp, ADD_VALUES));
    row = 9 * i + 3;
    PetscCall(MatSetValues(A, 1, &row, 1, &row, &atmp, ADD_VALUES));
    row = 9 * i + 6;
    PetscCall(MatSetValues(A, 1, &row, 1, &row, &atmp, ADD_VALUES));
    row = 9 * i + 7;
    PetscCall(MatSetValues(A, 1, &row, 1, &row, &atmp, ADD_VALUES));
    row = 9 * i + 8;
    PetscCall(MatSetValues(A, 1, &row, 1, &row, &atmp, ADD_VALUES));
  }
  PetscCall(MatAssemblyBegin(A, MAT_FINAL_ASSEMBLY));
  PetscCall(MatAssemblyEnd(A, MAT_FINAL_ASSEMBLY));
  PetscFunctionReturn(PETSC_SUCCESS);
}

int main(int argc, char **argv)
{
  TS                 ts;
  SNES               snes_alg;
  PetscMPIInt        size;
  Userctx            user;
  PetscViewer        Xview, Ybusview, viewer;
  Vec                X, F_alg;
  Mat                J, A;
  PetscInt           i, idx, *idx2;
  Vec                Xdot;
  PetscScalar       *x, *mat, *amat;
  const PetscScalar *rmat;
  Vec                vatol;
  PetscInt          *direction;
  PetscBool         *terminate;
  const PetscInt    *idx3;
  PetscScalar       *vatoli;
  PetscInt           k;

  PetscFunctionBeginUser;
  PetscCall(PetscInitialize(&argc, &argv, "petscoptions", help));
  PetscCallMPI(MPI_Comm_size(PETSC_COMM_WORLD, &size));
  PetscCheck(size == 1, PETSC_COMM_WORLD, PETSC_ERR_WRONG_MPI_SIZE, "Only for sequential runs");

  user.neqs_gen   = 9 * ngen; /* # eqs. for generator subsystem */
  user.neqs_net   = 2 * nbus; /* # eqs. for network subsystem   */
  user.neqs_pgrid = user.neqs_gen + user.neqs_net;

  /* Create indices for differential and algebraic equations */

  PetscCall(PetscMalloc1(7 * ngen, &idx2));
  for (i = 0; i < ngen; i++) {
    idx2[7 * i]     = 9 * i;
    idx2[7 * i + 1] = 9 * i + 1;
    idx2[7 * i + 2] = 9 * i + 2;
    idx2[7 * i + 3] = 9 * i + 3;
    idx2[7 * i + 4] = 9 * i + 6;
    idx2[7 * i + 5] = 9 * i + 7;
    idx2[7 * i + 6] = 9 * i + 8;
  }
  PetscCall(ISCreateGeneral(PETSC_COMM_WORLD, 7 * ngen, idx2, PETSC_COPY_VALUES, &user.is_diff));
  PetscCall(ISComplement(user.is_diff, 0, user.neqs_pgrid, &user.is_alg));
  PetscCall(PetscFree(idx2));

  /* Read initial voltage vector and Ybus */
  PetscCall(PetscViewerBinaryOpen(PETSC_COMM_WORLD, "X.bin", FILE_MODE_READ, &Xview));
  PetscCall(PetscViewerBinaryOpen(PETSC_COMM_WORLD, "Ybus.bin", FILE_MODE_READ, &Ybusview));

  PetscCall(VecCreate(PETSC_COMM_WORLD, &user.V0));
  PetscCall(VecSetSizes(user.V0, PETSC_DECIDE, user.neqs_net));
  PetscCall(VecLoad(user.V0, Xview));

  PetscCall(MatCreate(PETSC_COMM_WORLD, &user.Ybus));
  PetscCall(MatSetSizes(user.Ybus, PETSC_DECIDE, PETSC_DECIDE, user.neqs_net, user.neqs_net));
  PetscCall(MatSetType(user.Ybus, MATBAIJ));
  /*  PetscCall(MatSetBlockSize(user.Ybus,2)); */
  PetscCall(MatLoad(user.Ybus, Ybusview));

  /* Set run time options */
  PetscOptionsBegin(PETSC_COMM_WORLD, NULL, "Transient stability fault options", "");
  {
    user.tfaulton     = 1.0;
    user.tfaultoff    = 1.2;
    user.Rfault       = 0.0001;
    user.setisdiff    = PETSC_FALSE;
    user.semiexplicit = PETSC_FALSE;
    user.faultbus     = 8;
    PetscCall(PetscOptionsReal("-tfaulton", "", "", user.tfaulton, &user.tfaulton, NULL));
    PetscCall(PetscOptionsReal("-tfaultoff", "", "", user.tfaultoff, &user.tfaultoff, NULL));
    PetscCall(PetscOptionsInt("-faultbus", "", "", user.faultbus, &user.faultbus, NULL));
    user.t0   = 0.0;
    user.tmax = 5.0;
    PetscCall(PetscOptionsReal("-t0", "", "", user.t0, &user.t0, NULL));
    PetscCall(PetscOptionsReal("-tmax", "", "", user.tmax, &user.tmax, NULL));
    PetscCall(PetscOptionsBool("-setisdiff", "", "", user.setisdiff, &user.setisdiff, NULL));
    PetscCall(PetscOptionsBool("-dae_semiexplicit", "", "", user.semiexplicit, &user.semiexplicit, NULL));
  }
  PetscOptionsEnd();

  PetscCall(PetscViewerDestroy(&Xview));
  PetscCall(PetscViewerDestroy(&Ybusview));

  /* Create DMs for generator and network subsystems */
  PetscCall(DMDACreate1d(PETSC_COMM_WORLD, DM_BOUNDARY_NONE, user.neqs_gen, 1, 1, NULL, &user.dmgen));
  PetscCall(DMSetOptionsPrefix(user.dmgen, "dmgen_"));
  PetscCall(DMSetFromOptions(user.dmgen));
  PetscCall(DMSetUp(user.dmgen));
  PetscCall(DMDACreate1d(PETSC_COMM_WORLD, DM_BOUNDARY_NONE, user.neqs_net, 1, 1, NULL, &user.dmnet));
  PetscCall(DMSetOptionsPrefix(user.dmnet, "dmnet_"));
  PetscCall(DMSetFromOptions(user.dmnet));
  PetscCall(DMSetUp(user.dmnet));
  /* Create a composite DM packer and add the two DMs */
  PetscCall(DMCompositeCreate(PETSC_COMM_WORLD, &user.dmpgrid));
  PetscCall(DMSetOptionsPrefix(user.dmpgrid, "pgrid_"));
  PetscCall(DMCompositeAddDM(user.dmpgrid, user.dmgen));
  PetscCall(DMCompositeAddDM(user.dmpgrid, user.dmnet));

  PetscCall(DMCreateGlobalVector(user.dmpgrid, &X));

  PetscCall(MatCreate(PETSC_COMM_WORLD, &J));
  PetscCall(MatSetSizes(J, PETSC_DECIDE, PETSC_DECIDE, user.neqs_pgrid, user.neqs_pgrid));
  PetscCall(MatSetFromOptions(J));
  PetscCall(PreallocateJacobian(J, &user));

  /* Create matrix to save solutions at each time step */
  user.stepnum = 0;

  PetscCall(MatCreateSeqDense(PETSC_COMM_SELF, user.neqs_pgrid + 1, 1002, NULL, &user.Sol));
  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Create timestepping solver context
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  PetscCall(TSCreate(PETSC_COMM_WORLD, &ts));
  PetscCall(TSSetProblemType(ts, TS_NONLINEAR));
  if (user.semiexplicit) {
    PetscCall(TSSetType(ts, TSRK));
    PetscCall(TSSetRHSFunction(ts, NULL, RHSFunction, &user));
    PetscCall(TSSetRHSJacobian(ts, J, J, RHSJacobian, &user));
  } else {
    PetscCall(TSSetType(ts, TSCN));
    PetscCall(TSSetEquationType(ts, TS_EQ_DAE_IMPLICIT_INDEX1));
    PetscCall(TSSetIFunction(ts, NULL, (TSIFunctionFn *)IFunction, &user));
    PetscCall(TSSetIJacobian(ts, J, J, (TSIJacobianFn *)IJacobian, &user));
  }
  PetscCall(TSSetApplicationContext(ts, &user));

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Set initial conditions
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  PetscCall(SetInitialGuess(X, &user));
  /* Just to set up the Jacobian structure */

  PetscCall(VecDuplicate(X, &Xdot));
  PetscCall(IJacobian(ts, 0.0, X, Xdot, 0.0, J, J, &user));
  PetscCall(VecDestroy(&Xdot));

  /* Save initial solution */

  idx = user.stepnum * (user.neqs_pgrid + 1);
  PetscCall(MatDenseGetArray(user.Sol, &mat));
  PetscCall(VecGetArray(X, &x));

  mat[idx] = 0.0;

  PetscCall(PetscArraycpy(mat + idx + 1, x, user.neqs_pgrid));
  PetscCall(MatDenseRestoreArray(user.Sol, &mat));
  PetscCall(VecRestoreArray(X, &x));
  user.stepnum++;

  PetscCall(TSSetMaxTime(ts, user.tmax));
  PetscCall(TSSetExactFinalTime(ts, TS_EXACTFINALTIME_MATCHSTEP));
  PetscCall(TSSetTimeStep(ts, 0.01));
  PetscCall(TSSetFromOptions(ts));
  PetscCall(TSSetPostStep(ts, SaveSolution));
  PetscCall(TSSetSolution(ts, X));

  PetscCall(PetscMalloc1((2 * ngen + 2), &direction));
  PetscCall(PetscMalloc1((2 * ngen + 2), &terminate));
  direction[0] = direction[1] = 1;
  terminate[0] = terminate[1] = PETSC_FALSE;
  for (i = 0; i < ngen; i++) {
    direction[2 + 2 * i]     = -1;
    direction[2 + 2 * i + 1] = 1;
    terminate[2 + 2 * i] = terminate[2 + 2 * i + 1] = PETSC_FALSE;
  }

  PetscCall(TSSetEventHandler(ts, 2 * ngen + 2, direction, terminate, EventFunction, PostEventFunction, (void *)&user));

  if (user.semiexplicit) {
    /* Use a semi-explicit approach with the time-stepping done by an explicit method and the
       algrebraic part solved via PostStage and PostEvaluate callbacks
    */
    PetscCall(TSSetType(ts, TSRK));
    PetscCall(TSSetPostStage(ts, PostStage));
    PetscCall(TSSetPostEvaluate(ts, PostEvaluate));
  }

  if (user.setisdiff) {
    /* Create vector of absolute tolerances and set the algebraic part to infinity */
    PetscCall(VecDuplicate(X, &vatol));
    PetscCall(VecSet(vatol, 100000.0));
    PetscCall(VecGetArray(vatol, &vatoli));
    PetscCall(ISGetIndices(user.is_diff, &idx3));
    for (k = 0; k < 7 * ngen; k++) vatoli[idx3[k]] = 1e-2;
    PetscCall(VecRestoreArray(vatol, &vatoli));
  }

  /* Create the nonlinear solver for solving the algebraic system */
  /* Note that although the algebraic system needs to be solved only for
     Idq and V, we reuse the entire system including xgen. The xgen
     variables are held constant by setting their residuals to 0 and
     putting a 1 on the Jacobian diagonal for xgen rows
  */

  PetscCall(VecDuplicate(X, &F_alg));
  PetscCall(SNESCreate(PETSC_COMM_WORLD, &snes_alg));
  PetscCall(SNESSetFunction(snes_alg, F_alg, AlgFunction, &user));
  PetscCall(SNESSetJacobian(snes_alg, J, J, AlgJacobian, &user));
  PetscCall(SNESSetFromOptions(snes_alg));

  user.snes_alg = snes_alg;

  /* Solve */
  PetscCall(TSSolve(ts, X));

  PetscCall(MatAssemblyBegin(user.Sol, MAT_FINAL_ASSEMBLY));
  PetscCall(MatAssemblyEnd(user.Sol, MAT_FINAL_ASSEMBLY));

  PetscCall(MatCreateSeqDense(PETSC_COMM_SELF, user.neqs_pgrid + 1, user.stepnum, NULL, &A));
  PetscCall(MatDenseGetArrayRead(user.Sol, &rmat));
  PetscCall(MatDenseGetArray(A, &amat));
  PetscCall(PetscArraycpy(amat, rmat, user.stepnum * (user.neqs_pgrid + 1)));
  PetscCall(MatDenseRestoreArray(A, &amat));
  PetscCall(MatDenseRestoreArrayRead(user.Sol, &rmat));
  PetscCall(PetscViewerBinaryOpen(PETSC_COMM_SELF, "out.bin", FILE_MODE_WRITE, &viewer));
  PetscCall(MatView(A, viewer));
  PetscCall(PetscViewerDestroy(&viewer));
  PetscCall(MatDestroy(&A));

  PetscCall(PetscFree(direction));
  PetscCall(PetscFree(terminate));
  PetscCall(SNESDestroy(&snes_alg));
  PetscCall(VecDestroy(&F_alg));
  PetscCall(MatDestroy(&J));
  PetscCall(MatDestroy(&user.Ybus));
  PetscCall(MatDestroy(&user.Sol));
  PetscCall(VecDestroy(&X));
  PetscCall(VecDestroy(&user.V0));
  PetscCall(DMDestroy(&user.dmgen));
  PetscCall(DMDestroy(&user.dmnet));
  PetscCall(DMDestroy(&user.dmpgrid));
  PetscCall(ISDestroy(&user.is_diff));
  PetscCall(ISDestroy(&user.is_alg));
  PetscCall(TSDestroy(&ts));
  if (user.setisdiff) PetscCall(VecDestroy(&vatol));
  PetscCall(PetscFinalize());
  return 0;
}

/*TEST

   build:
      requires: double !complex !defined(PETSC_USE_64BIT_INDICES)

   test:
      suffix: implicit
      args: -ts_monitor -snes_monitor_short
      localrunfiles: petscoptions X.bin Ybus.bin

   test:
      suffix: semiexplicit
      args: -ts_monitor -snes_monitor_short -dae_semiexplicit -ts_rk_type 2a
      localrunfiles: petscoptions X.bin Ybus.bin

   test:
      suffix: steprestart
      # needs ARKIMEX methods with all implicit stages since the mass matrix is not the identity
      args: -ts_monitor -snes_monitor_short -ts_type arkimex -ts_arkimex_type prssp2
      localrunfiles: petscoptions X.bin Ybus.bin

TEST*/
