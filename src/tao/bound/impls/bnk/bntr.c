#include <../src/tao/bound/impls/bnk/bnk.h>
#include <petscksp.h>

/*
 Implements Newton's Method with a trust region approach for solving
 bound constrained minimization problems.
 
 ------------------------------------------------------------
 
 initialize trust radius (default: BNK_INIT_INTERPOLATION)
 x_0 = VecMedian(x_0)
 f_0, g_0 = TaoComputeObjectiveAndGradient(x_0)
 pg_0 = VecBoundGradientProjection(g_0)
 check convergence at pg_0
 niter = 0
 step_accepted = true
 
 while niter <= max_it
    if step_accepted
      niter += 1
      H_k = TaoComputeHessian(x_k)
      if pc_type == BNK_PC_BFGS
        add correction to BFGS approx
        if scale_type == BNK_SCALE_AHESS
          D = VecMedian(1e-6, abs(diag(H_k)), 1e6)
          scale BFGS with VecReciprocal(D)
        end
      end
    end

    if pc_type = BNK_PC_BFGS
      B_k = BFGS
    else
      B_k = VecMedian(1e-6, abs(diag(H_k)), 1e6)
      B_k = VecReciprocal(B_k)
    end
    w = x_k - VecMedian(x_k - 0.001*B_k*g_k)
    eps = min(eps, norm2(w))
    determine the active and inactive index sets such that
      L = {i : (x_k)_i <= l_i + eps && (g_k)_i > 0}
      U = {i : (x_k)_i >= u_i - eps && (g_k)_i < 0}
      F = {i : l_i = (x_k)_i = u_i}
      A = {L + U + F}
      I = {i : i not in A}
      
    generate the reduced system Hr_k dr_k = -gr_k for variables in I
    if pc_type == BNK_PC_BFGS && scale_type == BNK_SCALE_PHESS
      D = VecMedian(1e-6, abs(diag(Hr_k)), 1e6)
      scale BFGS with VecReciprocal(D)
    end
    solve Hr_k dr_k = -gr_k
    set d_k to (l - x) for variables in L, (u - x) for variables in U, and 0 for variables in F
    
    x_{k+1} = VecMedian(x_k + d_k)
    s = x_{k+1} - x_k
    prered = dot(s, 0.5*gr_k - Hr_k*s)
    f_{k+1} = TaoComputeObjective(x_{k+1})
    actred = f_k - f_{k+1}

    oldTrust = trust
    step_accepted, trust = TaoBNKUpdateTrustRadius(default: BNK_UPDATE_REDUCTION)
    if step_accepted
      g_{k+1} = TaoComputeGradient(x_{k+1})
      pg_{k+1} = VecBoundGradientProjection(g_{k+1})
      count the accepted Newton step
    else
      f_{k+1} = f_k
      x_{k+1} = x_k
      g_{k+1} = g_k
      pg_{k+1} = pg_k
      if trust == oldTrust
        terminate because we cannot shrink the radius any further
      end
    end 

    check convergence at pg_{k+1}
 end
*/

static PetscErrorCode TaoSolve_BNTR(Tao tao)
{
  PetscErrorCode               ierr;
  TAO_BNK                      *bnk = (TAO_BNK *)tao->data;
  KSPConvergedReason           ksp_reason;

  PetscReal                    resnorm, oldTrust, prered, actred, stepNorm, steplen;
  PetscBool                    stepAccepted = PETSC_TRUE, shift = PETSC_FALSE;
  PetscInt                     stepType = BNK_NEWTON;
  
  PetscFunctionBegin;
  /* Initialize the preconditioner, KSP solver and trust radius/line search */
  tao->reason = TAO_CONTINUE_ITERATING;
  ierr = TaoBNKInitialize(tao, bnk->init_type);CHKERRQ(ierr);
  if (tao->reason != TAO_CONTINUE_ITERATING) PetscFunctionReturn(0);

  /* Have not converged; continue with Newton method */
  while (tao->reason == TAO_CONTINUE_ITERATING) {
    
    if (stepAccepted) { 
      tao->niter++;
      tao->ksp_its=0;
      /* Compute the hessian and update the BFGS preconditioner at the new iterate*/
      ierr = TaoBNKComputeHessian(tao);CHKERRQ(ierr);
    }
    
    /* Use the common BNK kernel to compute the Newton step (for inactive variables only) */
    ierr = TaoBNKComputeStep(tao, shift, &ksp_reason);CHKERRQ(ierr);

    /* Store current solution before it changes */
    oldTrust = tao->trust;
    bnk->fold = bnk->f;
    ierr = VecCopy(tao->solution, bnk->Xold);CHKERRQ(ierr);
    ierr = VecCopy(tao->gradient, bnk->Gold);CHKERRQ(ierr);
    ierr = VecCopy(bnk->unprojected_gradient, bnk->unprojected_gradient_old);CHKERRQ(ierr);
    
    /* Temporarily accept the step and project it into the bounds */
    ierr = VecAXPY(tao->solution, 1.0, tao->stepdirection);CHKERRQ(ierr);
    ierr = VecMedian(tao->XL, tao->solution, tao->XU, tao->solution);CHKERRQ(ierr);
    
    /* Check if the projection changed the step direction */
    ierr = VecCopy(tao->solution, tao->stepdirection);CHKERRQ(ierr);
    ierr = VecAXPY(tao->stepdirection, -1.0, bnk->Xold);CHKERRQ(ierr);
    ierr = VecNorm(tao->stepdirection, NORM_2, &stepNorm);CHKERRQ(ierr);
    if (stepNorm != bnk->dnorm) {
      /* Projection changed the step, so we have to recompute predicted reduction.
         However, we deliberately do not change the step norm and the trust radius 
         in order for the safeguard to more closely mimic a piece-wise linesearch 
         along the bounds. */
      ierr = MatMult(bnk->H_inactive, tao->stepdirection, bnk->Xwork);CHKERRQ(ierr);
      ierr = VecAYPX(bnk->Xwork, -0.5, bnk->G_inactive);CHKERRQ(ierr);
      ierr = VecDot(bnk->Xwork, tao->stepdirection, &prered);
    } else {
      /* Step did not change, so we can just recover the pre-computed prediction */
      ierr = KSPCGGetObjFcn(tao->ksp, &prered);CHKERRQ(ierr);
    }
    prered = -prered;
    
    /* Compute the actual reduction and update the trust radius */
    ierr = TaoComputeObjective(tao, tao->solution, &bnk->f);CHKERRQ(ierr);
    actred = bnk->fold - bnk->f;
    ierr = TaoBNKUpdateTrustRadius(tao, prered, actred, bnk->update_type, stepType, &stepAccepted);CHKERRQ(ierr);
    
    if (stepAccepted) {
      /* Step is good, evaluate the gradient and the hessian */
      steplen = 1.0;
      ++bnk->newt;
      ierr = TaoComputeGradient(tao, tao->solution, bnk->unprojected_gradient);CHKERRQ(ierr);
      ierr = VecBoundGradientProjection(bnk->unprojected_gradient,tao->solution,tao->XL,tao->XU,tao->gradient);CHKERRQ(ierr);
      ierr = VecNorm(tao->gradient, NORM_2, &bnk->gnorm);CHKERRQ(ierr);
      if (PetscIsInfOrNanReal(bnk->gnorm)) SETERRQ(PETSC_COMM_SELF,1,"User provided compute function generated Not-a-Number");
    } else {
      /* Step is bad, revert old solution and re-solve with new radius*/
      steplen = 0.0;
      bnk->f = bnk->fold;
      ierr = VecCopy(bnk->Xold, tao->solution);CHKERRQ(ierr);
      ierr = VecCopy(bnk->Gold, tao->gradient);CHKERRQ(ierr);
      ierr = VecCopy(bnk->unprojected_gradient_old, bnk->unprojected_gradient);CHKERRQ(ierr);
      if (oldTrust == tao->trust) {
        /* Can't change the radius anymore so just terminate */
        tao->reason = TAO_DIVERGED_TR_REDUCTION;
      }
    }

    /*  Check for termination */
    ierr = VecFischer(tao->solution, bnk->unprojected_gradient, tao->XL, tao->XU, bnk->Gwork);CHKERRQ(ierr);
    ierr = VecNorm(bnk->Gwork, NORM_2, &resnorm);CHKERRQ(ierr);
    ierr = TaoLogConvergenceHistory(tao, bnk->f, resnorm, 0.0, tao->ksp_its);CHKERRQ(ierr);
    ierr = TaoMonitor(tao, tao->niter, bnk->f, resnorm, 0.0, steplen);CHKERRQ(ierr);
    ierr = (*tao->ops->convergencetest)(tao, tao->cnvP);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/

PETSC_INTERN PetscErrorCode TaoSetUp_BNTR(Tao tao)
{
  TAO_BNK        *bnk = (TAO_BNK *)tao->data;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = TaoSetUp_BNK(tao);CHKERRQ(ierr);
  if (!bnk->is_nash && !bnk->is_stcg && !bnk->is_gltr) SETERRQ(PETSC_COMM_SELF,1,"Must use a trust-region CG method for KSP (KSPNASH, KSPSTCG, KSPGLTR)");
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/

PETSC_INTERN PetscErrorCode TaoCreate_BNTR(Tao tao)
{
  TAO_BNK        *bnk;
  PetscErrorCode ierr;
  
  PetscFunctionBegin;
  ierr = TaoCreate_BNK(tao);CHKERRQ(ierr);
  tao->ops->solve=TaoSolve_BNTR;
  tao->ops->setup=TaoSetUp_BNTR;
  
  bnk = (TAO_BNK *)tao->data;
  bnk->update_type = BNK_UPDATE_REDUCTION; /* trust region updates based on predicted/actual reduction */
  bnk->sval = 0.0; /* disable Hessian shifting */
  PetscFunctionReturn(0);
}