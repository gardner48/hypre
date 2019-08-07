/*BHEADER**********************************************************************
 * Copyright (c) 2008,  Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * This file is part of HYPRE.  See file COPYRIGHT for details.
 *
 * HYPRE is free software; you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License (as published by the Free
 * Software Foundation) version 2.1 dated February 1999.
 *
 * $Revision$
 ***********************************************************************EHEADER*/




/******************************************************************************
 *
 * ILU solve routine
 *
 *****************************************************************************/
#include "_hypre_parcsr_ls.h"
#include "par_ilu.h"

/*--------------------------------------------------------------------
 * hypre_ILUSolve
 *--------------------------------------------------------------------*/
HYPRE_Int
hypre_ILUSolve( void               *ilu_vdata,
                  hypre_ParCSRMatrix *A,
                  hypre_ParVector    *f,
                  hypre_ParVector    *u )
{
   
   MPI_Comm             comm                 = hypre_ParCSRMatrixComm(A);
//   HYPRE_Int            i;
   
   hypre_ParILUData     *ilu_data            = (hypre_ParILUData*) ilu_vdata;

#ifdef HYPRE_USING_CUDA
   /* pointers to cusparse data, note that they are not NULL only when needed */
   cusparseMatDescr_t      matL_des          = hypre_ParILUDataMatLMatrixDescription(ilu_data);
   cusparseMatDescr_t      matU_des          = hypre_ParILUDataMatUMatrixDescription(ilu_data);
   void                    *ilu_solve_buffer = hypre_ParILUDataILUSolveBuffer(ilu_data);//device memory
   cusparseSolvePolicy_t   ilu_solve_policy  = hypre_ParILUDataILUSolvePolicy(ilu_data);
   hypre_CSRMatrix         *matBLU_d         = hypre_ParILUDataMatBILUDevice(ilu_data);
   hypre_CSRMatrix         *matE_d           = hypre_ParILUDataMatEDevice(ilu_data);
   hypre_CSRMatrix         *matF_d           = hypre_ParILUDataMatFDevice(ilu_data);
   csrsv2Info_t            matBL_info        = hypre_ParILUDataMatBLILUSolveInfo(ilu_data);
   csrsv2Info_t            matBU_info        = hypre_ParILUDataMatBUILUSolveInfo(ilu_data);
   csrsv2Info_t            matSL_info        = hypre_ParILUDataMatSLILUSolveInfo(ilu_data);
   csrsv2Info_t            matSU_info        = hypre_ParILUDataMatSUILUSolveInfo(ilu_data);
#endif
   
   /* get matrices */
   HYPRE_Int            ilu_type             = hypre_ParILUDataIluType(ilu_data);
   HYPRE_Int            *perm                = hypre_ParILUDataPerm(ilu_data);
   HYPRE_Int            *qperm               = hypre_ParILUDataQPerm(ilu_data);
   hypre_ParCSRMatrix   *matA                = hypre_ParILUDataMatA(ilu_data);
   hypre_ParCSRMatrix   *matL                = hypre_ParILUDataMatL(ilu_data);
   HYPRE_Real           *matD                = hypre_ParILUDataMatD(ilu_data);   
   hypre_ParCSRMatrix   *matU                = hypre_ParILUDataMatU(ilu_data);
   hypre_ParCSRMatrix   *matS                = hypre_ParILUDataMatS(ilu_data);
   
   HYPRE_Int            iter, num_procs,  my_id;

   hypre_ParVector      *F_array             = hypre_ParILUDataF(ilu_data);
   hypre_ParVector      *U_array             = hypre_ParILUDataU(ilu_data);

   /* get settings */
   HYPRE_Real           tol                  = hypre_ParILUDataTol(ilu_data);
   HYPRE_Int            logging              = hypre_ParILUDataLogging(ilu_data);
   HYPRE_Int            print_level          = hypre_ParILUDataPrintLevel(ilu_data);
   HYPRE_Int            max_iter             = hypre_ParILUDataMaxIter(ilu_data);
   HYPRE_Real           *norms               = hypre_ParILUDataRelResNorms(ilu_data);
   hypre_ParVector      *Ftemp               = hypre_ParILUDataFTemp(ilu_data);
   hypre_ParVector      *Utemp               = hypre_ParILUDataUTemp(ilu_data);
   HYPRE_Real           *fext                = hypre_ParILUDataFExt(ilu_data);
   HYPRE_Real           *uext                = hypre_ParILUDataUExt(ilu_data);
   hypre_ParVector      *residual;

   HYPRE_Real           alpha                = -1;
   HYPRE_Real           beta                 = 1;
   HYPRE_Real           conv_factor          = 0.0;
   HYPRE_Real           resnorm              = 1.0;
   HYPRE_Real           init_resnorm         = 0.0;
   HYPRE_Real           rel_resnorm;
   HYPRE_Real           rhs_norm             = 0.0;
   HYPRE_Real           old_resnorm;
   HYPRE_Real           ieee_check           = 0.0;
   HYPRE_Real           operat_cmplxty       = (ilu_data -> operator_complexity);

   HYPRE_Int            Solve_err_flag;
   
   /* problem size */
   HYPRE_Int            n                    = hypre_CSRMatrixNumRows(hypre_ParCSRMatrixDiag(A));
   HYPRE_Int            nLU                  = hypre_ParILUDataNLU(ilu_data);
   HYPRE_Int            *u_end               = hypre_ParILUDataUEnd(ilu_data);
   
   /* Schur system solve */
   HYPRE_Solver         schur_solver         = hypre_ParILUDataSchurSolver(ilu_data);
   HYPRE_Solver         schur_precond        = hypre_ParILUDataSchurPrecond(ilu_data);
   hypre_ParVector      *rhs                 = hypre_ParILUDataRhs(ilu_data);
   hypre_ParVector      *x                   = hypre_ParILUDataX(ilu_data);
   
   /* begin */
      
   if(logging > 1)
   {
      residual = (ilu_data -> residual);
   }

   (ilu_data -> num_iterations) = 0;

   hypre_MPI_Comm_size(comm, &num_procs);
   hypre_MPI_Comm_rank(comm,&my_id);

   /*-----------------------------------------------------------------------
    *    Write the solver parameters
    *-----------------------------------------------------------------------*/
   if (my_id == 0 && print_level > 1)
   {
      hypre_ILUWriteSolverParams(ilu_data);
   }

   /*-----------------------------------------------------------------------
    *    Initialize the solver error flag
    *-----------------------------------------------------------------------*/

   Solve_err_flag = 0;   
   /*-----------------------------------------------------------------------
    *     write some initial info
    *-----------------------------------------------------------------------*/

   if (my_id == 0 && print_level > 1 && tol > 0.)
   {
      hypre_printf("\n\n ILU SOLVER SOLUTION INFO:\n");
   }


   /*-----------------------------------------------------------------------
    *    Compute initial residual and print
    *-----------------------------------------------------------------------*/
   if (print_level > 1 || logging > 1 || tol > 0.)
   {
      if ( logging > 1 ) 
      {
         hypre_ParVectorCopy(f, residual );
         if (tol > 0.0)
         {
            hypre_ParCSRMatrixMatvec(alpha, A, u, beta, residual );
         }
         resnorm = sqrt(hypre_ParVectorInnerProd( residual, residual ));
      }
      else 
      {
         hypre_ParVectorCopy(f, Ftemp);
         if (tol > 0.0)
         {
            hypre_ParCSRMatrixMatvec(alpha, A, u, beta, Ftemp);
         }
         resnorm = sqrt(hypre_ParVectorInnerProd(Ftemp, Ftemp));
      }

      /* Since it is does not diminish performance, attempt to return an error flag
         and notify users when they supply bad input. */
      if (resnorm != 0.) 
      {
         ieee_check = resnorm/resnorm; /* INF -> NaN conversion */
      }
      if (ieee_check != ieee_check)
      {
         /* ...INFs or NaNs in input can make ieee_check a NaN.  This test
           for ieee_check self-equality works on all IEEE-compliant compilers/
           machines, c.f. page 8 of "Lecture Notes on the Status of IEEE 754"
           by W. Kahan, May 31, 1996.  Currently (July 2002) this paper may be
           found at http://HTTP.CS.Berkeley.EDU/~wkahan/ieee754status/IEEE754.PDF */
         if (print_level > 0)
         {
            hypre_printf("\n\nERROR detected by Hypre ...  BEGIN\n");
            hypre_printf("ERROR -- hypre_ILUSolve: INFs and/or NaNs detected in input.\n");
            hypre_printf("User probably placed non-numerics in supplied A, x_0, or b.\n");
            hypre_printf("ERROR detected by Hypre ...  END\n\n\n");
         }
         hypre_error(HYPRE_ERROR_GENERIC);
         return hypre_error_flag;
      }

      init_resnorm = resnorm;
      rhs_norm = sqrt(hypre_ParVectorInnerProd(f, f));
      if (rhs_norm)
      {
         rel_resnorm = init_resnorm / rhs_norm;
      }
      else
      {
         /* rhs is zero, return a zero solution */
         hypre_ParVectorSetConstantValues(U_array, 0.0);
         if(logging > 0)
         {
            rel_resnorm = 0.0;
            (ilu_data -> final_rel_residual_norm) = rel_resnorm;
         }
         return hypre_error_flag;
      }
   }
   else
   {
      rel_resnorm = 1.;
   }

   if (my_id == 0 && print_level > 1)
   {
      hypre_printf("                                            relative\n");
      hypre_printf("               residual        factor       residual\n");
      hypre_printf("               --------        ------       --------\n");
      hypre_printf("    Initial    %e                 %e\n",init_resnorm,
              rel_resnorm);
   }

   matA = A;
   U_array = u;
   F_array = f;

   /************** Main Solver Loop - always do 1 iteration ************/
   iter = 0;
   
   while ((rel_resnorm >= tol || iter < 1)
          && iter < max_iter)
   {

      /* Do one solve on LUe=r */
      switch(ilu_type){
      case 0: case 1: 
#ifdef HYPRE_USING_CUDA
         if(ilu_type == 0 && hypre_ParILUDataLfil(ilu_data) == 0)
         {
            /* Apply GPU-accelerated LU solve */
            hypre_ILUSolveCusparseLU(matA, matL_des, matU_des, matBL_info, matBU_info, matBLU_d, ilu_solve_policy, 
                                       ilu_solve_buffer, f, u, perm, n, Utemp, Ftemp);//BJ-cusparse
         }
         else
         {
#endif
            hypre_ILUSolveLU(matA, f, u, perm, n, matL, matD, matU, Utemp, Ftemp); //BJ
#ifdef HYPRE_USING_CUDA
         }
#endif
         break;
      case 10: case 11:
#ifdef HYPRE_USING_CUDA
         if(ilu_type == 10 && hypre_ParILUDataLfil(ilu_data) == 0)
         {
            /* Apply GPU-accelerated LU solve */
            hypre_ILUSolveCusparseSchurGMRES(matA, f, u, perm, nLU, matS, Utemp, Ftemp, schur_solver, schur_precond, rhs, x, u_end,
                                             matL_des, matU_des, matBL_info, matBU_info, matSL_info, matSU_info,
                                             matBLU_d, matE_d, matF_d, ilu_solve_policy, ilu_solve_buffer);//GMRES-cusparse
         }
         else
         {
#endif
            hypre_ILUSolveSchurGMRES(matA, f, u, perm, perm, nLU, matL, matD, matU, matS, 
                              Utemp, Ftemp, schur_solver, schur_precond, rhs, x, u_end); //GMRES
#ifdef HYPRE_USING_CUDA
         }
#endif
         break;
      case 20: case 21:
         hypre_ILUSolveSchurNSH(matA, f, u, perm, nLU, matL, matD, matU, matS, 
                           Utemp, Ftemp, schur_solver, rhs, x, u_end); //MR+NSH
         break;
      case 30: case 31:
         hypre_ILUSolveLURAS(matA, f, u, perm, matL, matD, matU, Utemp, Utemp, fext, uext); //RAS
         break;
      case 40: case 41: 
         hypre_ILUSolveSchurGMRES(matA, f, u, perm, qperm, nLU, matL, matD, matU, matS, 
                           Utemp, Ftemp, schur_solver, schur_precond, rhs, x, u_end); //GMRES
         break;
      default: 
#ifdef HYPRE_USING_CUDA
         /* Apply GPU-accelerated LU solve */
         hypre_ILUSolveCusparseLU(matA, matL_des, matU_des, matBL_info, matBU_info, matBLU_d, ilu_solve_policy, 
                                    ilu_solve_buffer, f, u, perm, n, Utemp, Ftemp);//BJ-cusparse
#else
         hypre_ILUSolveLU(matA, f, u, perm, n, matL, matD, matU, Utemp, Ftemp); //BJ
#endif
         break;

        }

      /*---------------------------------------------------------------
       *    Compute residual and residual norm
       *----------------------------------------------------------------*/

      if (print_level > 1 || logging > 1 || tol > 0.)
      {
        old_resnorm = resnorm;

        if ( logging > 1 ) {
           hypre_ParVectorCopy(F_array, residual);
           hypre_ParCSRMatrixMatvec(alpha, matA, U_array, beta, residual );
           resnorm = sqrt(hypre_ParVectorInnerProd( residual, residual ));
        }
        else {
           hypre_ParVectorCopy(F_array, Ftemp);
           hypre_ParCSRMatrixMatvec(alpha, matA, U_array, beta, Ftemp);
           resnorm = sqrt(hypre_ParVectorInnerProd(Ftemp, Ftemp));
        }

        if (old_resnorm) conv_factor = resnorm / old_resnorm;
        else conv_factor = resnorm;
        if (rhs_norm)
        {
           rel_resnorm = resnorm / rhs_norm;
        }
        else
        {
           rel_resnorm = resnorm;
        }

        norms[iter] = rel_resnorm;
      }

      ++iter;
      (ilu_data -> num_iterations) = iter;
      (ilu_data -> final_rel_residual_norm) = rel_resnorm;

      if (my_id == 0 && print_level > 1)
      {
         hypre_printf("    ILUSolve %2d   %e    %f     %e \n", iter,
                 resnorm, conv_factor, rel_resnorm);
      }
   }

   /* check convergence within max_iter */
   if (iter == max_iter && tol > 0.)
   {
      Solve_err_flag = 1;
      hypre_error(HYPRE_ERROR_CONV);
   }

   /*-----------------------------------------------------------------------
    *    Print closing statistics
    *    Add operator and grid complexity stats
    *-----------------------------------------------------------------------*/

   if (iter > 0 && init_resnorm)
   {
      conv_factor = pow((resnorm/init_resnorm),(1.0/(HYPRE_Real) iter));
   }
   else
   {
      conv_factor = 1.;
   }

   if (print_level > 1)
   {
      /*** compute operator and grid complexity (fill factor) here ?? ***/
      if (my_id == 0)
      {
         if (Solve_err_flag == 1)
         {
            hypre_printf("\n\n==============================================");
            hypre_printf("\n NOTE: Convergence tolerance was not achieved\n");
            hypre_printf("      within the allowed %d iterations\n",max_iter);
            hypre_printf("==============================================");
         }
         hypre_printf("\n\n Average Convergence Factor = %f \n",conv_factor);
         hypre_printf("                operator = %f\n",operat_cmplxty);
      }
   }
   return hypre_error_flag;
}

/* Schur Complement solve with GMRES on schur complement
 * ParCSRMatrix S is already built in ilu data sturcture, here directly use S
 * L, D and U factors only have local scope (no off-diagonal processor terms)
 * so apart from the residual calculation (which uses A), the solves with the 
 * L and U factors are local.
 * S is the global Schur complement
 * schur_solver is a GMRES solver
 * schur_precond is the ILU preconditioner for GMRES
 * rhs and x are helper vector for solving Schur system
*/

HYPRE_Int
hypre_ILUSolveSchurGMRES(hypre_ParCSRMatrix *A, hypre_ParVector    *f,
                  hypre_ParVector    *u, HYPRE_Int *perm, HYPRE_Int *qperm,
                  HYPRE_Int nLU, hypre_ParCSRMatrix *L, 
                  HYPRE_Real* D, hypre_ParCSRMatrix *U,
                  hypre_ParCSRMatrix *S,
                  hypre_ParVector *ftemp, hypre_ParVector *utemp,
                  HYPRE_Solver schur_solver, HYPRE_Solver schur_precond,
                  hypre_ParVector *rhs, hypre_ParVector *x, HYPRE_Int *u_end)
{
   /* data objects for communication */
//   MPI_Comm          comm = hypre_ParCSRMatrixComm(A);
   
   /* data objects for L and U */
   hypre_CSRMatrix   *L_diag = hypre_ParCSRMatrixDiag(L);
   HYPRE_Real        *L_diag_data = hypre_CSRMatrixData(L_diag);
   HYPRE_Int         *L_diag_i = hypre_CSRMatrixI(L_diag);
   HYPRE_Int         *L_diag_j = hypre_CSRMatrixJ(L_diag);
   hypre_CSRMatrix   *U_diag = hypre_ParCSRMatrixDiag(U);
   HYPRE_Real        *U_diag_data = hypre_CSRMatrixData(U_diag);
   HYPRE_Int         *U_diag_i = hypre_CSRMatrixI(U_diag);
   HYPRE_Int         *U_diag_j = hypre_CSRMatrixJ(U_diag);
   hypre_Vector      *utemp_local = hypre_ParVectorLocalVector(utemp);
   HYPRE_Real        *utemp_data  = hypre_VectorData(utemp_local);
   hypre_Vector      *ftemp_local = hypre_ParVectorLocalVector(ftemp);
   HYPRE_Real        *ftemp_data  = hypre_VectorData(ftemp_local);      

   HYPRE_Real        alpha;
   HYPRE_Real        beta;   
   HYPRE_Int         i, j, k1, k2, col;
   
   /* problem size */
   HYPRE_Int         n = hypre_CSRMatrixNumRows(L_diag);
//   HYPRE_Int         m = n - nLU;
   
   /* other data objects for computation */
   //hypre_Vector      *f_local;
   //HYPRE_Real        *f_data;
   hypre_Vector      *rhs_local;
   HYPRE_Real        *rhs_data;
   hypre_Vector      *x_local;
   HYPRE_Real        *x_data;
   
   /* begin */
   beta = 1.0;
   alpha = -1.0;
   
   /* compute residual */
   hypre_ParCSRMatrixMatvecOutOfPlace(alpha, A, u, beta, f, ftemp);
   
   /* 1st need to solve LBi*xi = fi  
    * L solve, solve xi put in u_temp upper 
    */
   //f_local = hypre_ParVectorLocalVector(f);
   //f_data = hypre_VectorData(f_local);
   /* now update with L to solve */
   for(i = 0 ; i < nLU ; i ++)
   {
      utemp_data[qperm[i]] = ftemp_data[perm[i]];
      k1 = L_diag_i[i] ; k2 = L_diag_i[i+1];
      for(j = k1 ; j < k2 ; j ++)
      {
         utemp_data[qperm[i]] -= L_diag_data[j] * utemp_data[qperm[L_diag_j[j]]];
      }
   }
   
   /* 2nd need to compute g'i = gi - Ei*UBi^-1*xi
    * now put g'i into the f_temp lower 
    */
   for(i = nLU ; i < n ; i ++)
   {
      k1 = L_diag_i[i] ; k2 = L_diag_i[i+1];
      for(j = k1 ; j < k2 ; j ++)
      {
         col = L_diag_j[j];
         ftemp_data[perm[i]] -= L_diag_data[j] * utemp_data[qperm[col]];
      }
   }
   
   /* 3rd need to solve global Schur Complement Sy = g'
    * for now only solve the local system
    * solve y put in u_temp lower
    * only solve whe S is not NULL 
    */
   if(S)
   {
      /* setup vectors for solve */
      rhs_local   = hypre_ParVectorLocalVector(rhs);
      rhs_data    = hypre_VectorData(rhs_local);
      x_local     = hypre_ParVectorLocalVector(x);
      x_data      = hypre_VectorData(x_local);
      
      /* set rhs value */
      for(i = nLU ; i < n ; i ++)
      {
         rhs_data[i-nLU] = ftemp_data[perm[i]];
      }
      
      /* solve */
      HYPRE_GMRESSolve(schur_solver,(HYPRE_Matrix)S,(HYPRE_Vector)rhs,(HYPRE_Vector)x);

      /* copy value back to original */
      for(i = nLU ; i < n ; i ++)
      {
         utemp_data[qperm[i]] = x_data[i-nLU];
      }
   }
   
   /* 4th need to compute zi = xi - LBi^-1*Fi*yi
    * put zi in f_temp upper 
    * only do this computation when nLU < n
    * U is unsorted, search is expensive when unnecessary
    */
   if(nLU < n)
   {
      for(i = 0 ; i < nLU ; i ++)
      {
         ftemp_data[perm[i]] = utemp_data[qperm[i]];
         k1 = u_end[i] ; k2 = U_diag_i[i+1];
         for(j = k1 ; j < k2 ; j ++)
         {
            col = U_diag_j[j];
            ftemp_data[perm[i]] -= U_diag_data[j] * utemp_data[qperm[col]];
         }
      }
      for(i = 0 ; i < nLU ; i ++)
      {
         utemp_data[qperm[i]] = ftemp_data[perm[i]];
      }
   }
   
   /* 5th need to solve UBi*ui = zi */
   /* put result in u_temp upper */
   for(i = nLU-1 ; i >= 0 ; i --)
   {
      k1 = U_diag_i[i] ; k2 = u_end[i];
      for(j = k1 ; j < k2 ; j ++)
      {
         col = U_diag_j[j];
         utemp_data[qperm[i]] -= U_diag_data[j] * utemp_data[qperm[col]];
      }
      utemp_data[qperm[i]] *= D[i];
   }
   
   /* done, now everything are in u_temp, update solution */
   hypre_ParVectorAxpy(beta, utemp, u);
             
   return hypre_error_flag;
}

/* Newton-Schulz-Hotelling solve
 * ParCSRMatrix S is already built in ilu data sturcture
 * S here is the INVERSE of Schur Complement
 * L, D and U factors only have local scope (no off-diagonal processor terms)
 * so apart from the residual calculation (which uses A), the solves with the 
 * L and U factors are local.
 * S is the inverse global Schur complement
 * rhs and x are helper vector for solving Schur system
*/

HYPRE_Int
hypre_ILUSolveSchurNSH(hypre_ParCSRMatrix *A, hypre_ParVector    *f,
                  hypre_ParVector    *u, HYPRE_Int *perm, 
                  HYPRE_Int nLU, hypre_ParCSRMatrix *L, 
                  HYPRE_Real* D, hypre_ParCSRMatrix *U,
                  hypre_ParCSRMatrix *S,
                  hypre_ParVector *ftemp, hypre_ParVector *utemp,
                  HYPRE_Solver schur_solver,
                  hypre_ParVector *rhs, hypre_ParVector *x, HYPRE_Int *u_end)
{
   /* data objects for communication */
//   MPI_Comm          comm = hypre_ParCSRMatrixComm(A); 
   /* data objects for L and U */
   hypre_CSRMatrix   *L_diag = hypre_ParCSRMatrixDiag(L);
   HYPRE_Real        *L_diag_data = hypre_CSRMatrixData(L_diag);
   HYPRE_Int         *L_diag_i = hypre_CSRMatrixI(L_diag);
   HYPRE_Int         *L_diag_j = hypre_CSRMatrixJ(L_diag);
   hypre_CSRMatrix   *U_diag = hypre_ParCSRMatrixDiag(U);
   HYPRE_Real        *U_diag_data = hypre_CSRMatrixData(U_diag);
   HYPRE_Int         *U_diag_i = hypre_CSRMatrixI(U_diag);
   HYPRE_Int         *U_diag_j = hypre_CSRMatrixJ(U_diag);
   hypre_Vector      *utemp_local = hypre_ParVectorLocalVector(utemp);
   HYPRE_Real        *utemp_data  = hypre_VectorData(utemp_local);
   hypre_Vector      *ftemp_local = hypre_ParVectorLocalVector(ftemp);
   HYPRE_Real        *ftemp_data  = hypre_VectorData(ftemp_local);      

   HYPRE_Real        alpha;
   HYPRE_Real        beta;   
   HYPRE_Int         i, j, k1, k2, col;
   
   /* problem size */
   HYPRE_Int         n = hypre_CSRMatrixNumRows(L_diag);
//   HYPRE_Int         m = n - nLU;
   
   /* other data objects for computation */
   //hypre_Vector      *f_local;
   //HYPRE_Real        *f_data;
   hypre_Vector      *rhs_local;
   HYPRE_Real        *rhs_data;
   hypre_Vector      *x_local;
   HYPRE_Real        *x_data;
   /* begin */
   beta = 1.0;
   alpha = -1.0;
   
   /* compute residual */
   hypre_ParCSRMatrixMatvecOutOfPlace(alpha, A, u, beta, f, ftemp);
   /* 1st need to solve LBi*xi = fi  
    * L solve, solve xi put in u_temp upper 
    */
   //f_local = hypre_ParVectorLocalVector(f);
   //f_data = hypre_VectorData(f_local);
   /* now update with L to solve */
   for(i = 0 ; i < nLU ; i ++)
   {
      utemp_data[perm[i]] = ftemp_data[perm[i]];
      k1 = L_diag_i[i] ; k2 = L_diag_i[i+1];
      for(j = k1 ; j < k2 ; j ++)
      {
         utemp_data[perm[i]] -= L_diag_data[j] * utemp_data[perm[L_diag_j[j]]];
      }
   }
   
   /* 2nd need to compute g'i = gi - Ei*UBi^-1*xi
    * now put g'i into the f_temp lower 
    */
   for(i = nLU ; i < n ; i ++)
   {
      k1 = L_diag_i[i] ; k2 = L_diag_i[i+1];
      for(j = k1 ; j < k2 ; j ++)
      {
         col = L_diag_j[j];
         ftemp_data[perm[i]] -= L_diag_data[j] * utemp_data[perm[col]];
      }
   }
   
   /* 3rd need to solve global Schur Complement Sy = g'
    * for now only solve the local system
    * solve y put in u_temp lower
    * only solve whe S is not NULL 
    */
   if(S)
   {
      /* setup vectors for solve */
      rhs_local   = hypre_ParVectorLocalVector(rhs);
      rhs_data    = hypre_VectorData(rhs_local);
      x_local     = hypre_ParVectorLocalVector(x);
      x_data      = hypre_VectorData(x_local);
      
      /* set rhs value */
      for(i = nLU ; i < n ; i ++)
      {
         rhs_data[i-nLU] = ftemp_data[perm[i]];
      }
      
      /* Solve Schur system with approx inverse
       * x = S*rhs
       */
      hypre_NSHSolve(schur_solver,S,rhs,x);
      
      /* copy value back to original */
      for(i = nLU ; i < n ; i ++)
      {
         utemp_data[perm[i]] = x_data[i-nLU];
      }
   } 
   /* 4th need to compute zi = xi - LBi^-1*yi
    * put zi in f_temp upper 
    * only do this computation when nLU < n
    * U is unsorted, search is expensive when unnecessary
    */
   if(nLU < n)
   {
      for(i = 0 ; i < nLU ; i ++)
      {
         ftemp_data[perm[i]] = utemp_data[perm[i]];
         k1 = u_end[i] ; k2 = U_diag_i[i+1];
         for(j = k1 ; j < k2 ; j ++)
         {
            col = U_diag_j[j];
            ftemp_data[perm[i]] -= U_diag_data[j] * utemp_data[perm[col]];
         }
      }
      for(i = 0 ; i < nLU ; i ++)
      {
         utemp_data[perm[i]] = ftemp_data[perm[i]];
      }
   } 
   /* 5th need to solve UBi*ui = zi */
   /* put result in u_temp upper */
   for(i = nLU-1 ; i >= 0 ; i --)
   {
      k1 = U_diag_i[i] ; k2 = u_end[i];
      for(j = k1 ; j < k2 ; j ++)
      {
         col = U_diag_j[j];
         utemp_data[perm[i]] -= U_diag_data[j] * utemp_data[perm[col]];
      }
      utemp_data[perm[i]] *= D[i];
   }
   
   /* done, now everything are in u_temp, update solution */
   hypre_ParVectorAxpy(beta, utemp, u);
             
   return hypre_error_flag;
}

/* Incomplete LU solve
 * L, D and U factors only have local scope (no off-diagonal processor terms)
 * so apart from the residual calculation (which uses A), the solves with the 
 * L and U factors are local.
*/

HYPRE_Int
hypre_ILUSolveLU(hypre_ParCSRMatrix *A, hypre_ParVector    *f,
                  hypre_ParVector    *u, HYPRE_Int *perm, 
                  HYPRE_Int nLU, hypre_ParCSRMatrix *L, 
                  HYPRE_Real* D, hypre_ParCSRMatrix *U,
                  hypre_ParVector *ftemp, hypre_ParVector *utemp)
{
   hypre_CSRMatrix *L_diag = hypre_ParCSRMatrixDiag(L);
   HYPRE_Real      *L_diag_data = hypre_CSRMatrixData(L_diag);
   HYPRE_Int       *L_diag_i = hypre_CSRMatrixI(L_diag);
   HYPRE_Int       *L_diag_j = hypre_CSRMatrixJ(L_diag);

   hypre_CSRMatrix *U_diag = hypre_ParCSRMatrixDiag(U);
   HYPRE_Real      *U_diag_data = hypre_CSRMatrixData(U_diag);
   HYPRE_Int       *U_diag_i = hypre_CSRMatrixI(U_diag);
   HYPRE_Int       *U_diag_j = hypre_CSRMatrixJ(U_diag);
   
   hypre_Vector    *utemp_local = hypre_ParVectorLocalVector(utemp);
   HYPRE_Real      *utemp_data  = hypre_VectorData(utemp_local);
   
   hypre_Vector    *ftemp_local = hypre_ParVectorLocalVector(ftemp);
   HYPRE_Real      *ftemp_data  = hypre_VectorData(ftemp_local);      

   HYPRE_Real      alpha;
   HYPRE_Real      beta;   
   HYPRE_Int       i, j, k1, k2;

   /* begin */
   alpha = -1.0;
   beta = 1.0;

   /* Initialize Utemp to zero. 
    * This is necessary for correctness, when we use optimized 
    * vector operations in the case where sizeof(L, D or U) < sizeof(A)
   */
   //hypre_ParVectorSetConstantValues( utemp, 0.);
   /* compute residual */
   hypre_ParCSRMatrixMatvecOutOfPlace(alpha, A, u, beta, f, ftemp);
   
   /* L solve - Forward solve */
   /* copy rhs to account for diagonal of L (which is identity) */
   for( i = 0; i < nLU; i++ )
   {
      utemp_data[perm[i]] = ftemp_data[perm[i]];  
   } 
   /* update with remaining (off-diagonal) entries of L */     
   for( i = 0; i < nLU; i++ ) 
   {
      k1 = L_diag_i[i] ; k2 = L_diag_i[i+1];
      for(j=k1; j <k2; j++) 
      {
         utemp_data[perm[i]] -= L_diag_data[j] * utemp_data[perm[L_diag_j[j]]];
      }
   }
   /*-------------------- U solve - Backward substitution */    
   for( i = nLU-1; i >= 0; i-- ) 
   {
      /* first update with the remaining (off-diagonal) entries of U */
      k1 = U_diag_i[i] ; k2 = U_diag_i[i+1];
      for(j=k1; j <k2; j++) 
      {
         utemp_data[perm[i]] -= U_diag_data[j] * utemp_data[perm[U_diag_j[j]]];
      }
      /* diagonal scaling (contribution from D. Note: D is stored as its inverse) */
      utemp_data[perm[i]] *= D[i];        
   }   
   
   /* Update solution */
   hypre_ParVectorAxpy(beta, utemp, u);
   
   
   return hypre_error_flag;
}


/* Incomplete LU solve RAS
 * L, D and U factors only have local scope (no off-diagonal processor terms)
 * so apart from the residual calculation (which uses A), the solves with the 
 * L and U factors are local.
 * fext and uext are tempory arrays for external data
*/

HYPRE_Int
hypre_ILUSolveLURAS(hypre_ParCSRMatrix *A, hypre_ParVector    *f,
                  hypre_ParVector    *u, HYPRE_Int *perm, 
                  hypre_ParCSRMatrix *L, 
                  HYPRE_Real* D, hypre_ParCSRMatrix *U,
                  hypre_ParVector *ftemp, hypre_ParVector *utemp,
                  HYPRE_Real *fext, HYPRE_Real *uext)
{
   
   hypre_ParCSRCommPkg        *comm_pkg;
   hypre_ParCSRCommHandle     *comm_handle;
   HYPRE_Int                  num_sends, begin, end;
   
   hypre_CSRMatrix            *L_diag = hypre_ParCSRMatrixDiag(L);
   HYPRE_Real                 *L_diag_data = hypre_CSRMatrixData(L_diag);
   HYPRE_Int                  *L_diag_i = hypre_CSRMatrixI(L_diag);
   HYPRE_Int                  *L_diag_j = hypre_CSRMatrixJ(L_diag);

   hypre_CSRMatrix            *U_diag = hypre_ParCSRMatrixDiag(U);
   HYPRE_Real                 *U_diag_data = hypre_CSRMatrixData(U_diag);
   HYPRE_Int                  *U_diag_i = hypre_CSRMatrixI(U_diag);
   HYPRE_Int                  *U_diag_j = hypre_CSRMatrixJ(U_diag);
   
   HYPRE_Int                  n = hypre_CSRMatrixNumCols(hypre_ParCSRMatrixDiag(A));
   HYPRE_Int                  m = hypre_CSRMatrixNumCols(hypre_ParCSRMatrixOffd(A));
//   HYPRE_Int                  buffer_size;
   HYPRE_Int                  n_total = m + n;
   
   HYPRE_Int                  idx;
   HYPRE_Int                  jcol;
   HYPRE_Int                  col;
   
   hypre_Vector               *utemp_local = hypre_ParVectorLocalVector(utemp);
   HYPRE_Real                 *utemp_data  = hypre_VectorData(utemp_local);
   
   hypre_Vector               *ftemp_local = hypre_ParVectorLocalVector(ftemp);
   HYPRE_Real                 *ftemp_data  = hypre_VectorData(ftemp_local);      

   HYPRE_Real                 alpha;
   HYPRE_Real                 beta;   
   HYPRE_Int                  i, j, k1, k2;
   
   /* begin */
   alpha = -1.0;
   beta = 1.0;
   
   /* prepare for communication */
   comm_pkg = hypre_ParCSRMatrixCommPkg(A);
   /* setup if not yet built */
   if(!comm_pkg)
   {
      hypre_MatvecCommPkgCreate(A);
      comm_pkg = hypre_ParCSRMatrixCommPkg(A);
   }

   /* Initialize Utemp to zero. 
    * This is necessary for correctness, when we use optimized 
    * vector operations in the case where sizeof(L, D or U) < sizeof(A)
   */
   //hypre_ParVectorSetConstantValues( utemp, 0.);
   /* compute residual */
   hypre_ParCSRMatrixMatvecOutOfPlace(alpha, A, u, beta, f, ftemp);
   
   /* communication to get external data */
   
   /* get total num of send */
   num_sends = hypre_ParCSRCommPkgNumSends(comm_pkg);
   begin = hypre_ParCSRCommPkgSendMapStart(comm_pkg,0);
   end = hypre_ParCSRCommPkgSendMapStart(comm_pkg,num_sends);
   
   /* copy new index into send_buf */
   for(i = begin ; i < end ; i ++)
   {
      /* all we need is just send out data, we don't need to worry about the
       *    permutation of offd part, actually we don't need to worry about
       *    permutation at all
       * borrow uext as send buffer .
       */
      uext[i-begin] = ftemp_data[hypre_ParCSRCommPkgSendMapElmt(comm_pkg,i)]; 
   }
         
   /* main communication */
   comm_handle = hypre_ParCSRCommHandleCreate(1, comm_pkg, uext, fext);
   hypre_ParCSRCommHandleDestroy(comm_handle);
   
   /* L solve - Forward solve */
   for( i = 0 ; i < n_total ; i ++)
   {
      k1 = L_diag_i[i] ; k2 = L_diag_i[i+1];
      if( i < n )
      {
         /* diag part */
         utemp_data[perm[i]] = ftemp_data[perm[i]];
         for(j=k1; j <k2; j++) 
         {
            col = L_diag_j[j];
            if( col < n )
            {
               utemp_data[perm[i]] -= L_diag_data[j] * utemp_data[perm[col]];
            }
            else
            {
               jcol = col - n;
               utemp_data[perm[i]] -= L_diag_data[j] * uext[jcol];
            }
         }
      }
      else
      {
         /* offd part */
         idx = i - n;
         uext[idx] = fext[idx];
         for(j=k1; j <k2; j++) 
         {
            col = L_diag_j[j];
            if(col < n)
            {
               uext[idx] -= L_diag_data[j] * utemp_data[perm[col]];
            }
            else
            {
               jcol = col - n;
               uext[idx] -= L_diag_data[j] * uext[jcol];
            }
         }
      }
   }
   
   /*-------------------- U solve - Backward substitution */    
   for( i = n_total-1; i >= 0; i-- ) 
   {
      /* first update with the remaining (off-diagonal) entries of U */
      k1 = U_diag_i[i] ; k2 = U_diag_i[i+1];
      if( i < n )
      {
         /* diag part */
         for(j=k1; j <k2; j++) 
         {
            col = U_diag_j[j];
            if( col < n )
            {
               utemp_data[perm[i]] -= U_diag_data[j] * utemp_data[perm[col]];
            }
            else
            {
               jcol = col - n;
               utemp_data[perm[i]] -= U_diag_data[j] * uext[jcol];
            }
         }
         /* diagonal scaling (contribution from D. Note: D is stored as its inverse) */
         utemp_data[perm[i]] *= D[i];
      }
      else
      {
         /* 2nd part of offd */
         idx = i - n;
         for(j=k1; j <k2; j++) 
         {
            col = U_diag_j[j];
            if( col < n )
            {
               uext[idx] -= U_diag_data[j] * utemp_data[perm[col]];
            }
            else
            {
               jcol = col - n;
               uext[idx] -= U_diag_data[j] * uext[jcol];
            }
         }
         /* diagonal scaling (contribution from D. Note: D is stored as its inverse) */
         uext[idx] *= D[i];
      }
             
   }   
   /* Update solution */
   hypre_ParVectorAxpy(beta, utemp, u);
   
   return hypre_error_flag;
}

#ifdef HYPRE_USING_CUDA

/* Permutation function (for GPU version, can just call thrust)
 * option 00: perm integer array
 * option 01: rperm integer array
 * option 10: perm real array
 * option 11: rperm real array
 * */
HYPRE_Int
hypre_ILUSeqVectorPerm(void *vectori, void *vectoro, HYPRE_Int size, HYPRE_Int *perm, HYPRE_Int option)
{
   cudaDeviceSynchronize();
   HYPRE_Int i;
   switch(option)
   {
      case 00:
      {
         HYPRE_Int *ivectori     = (HYPRE_Int *) vectori;
         HYPRE_Int *ivectoro     = (HYPRE_Int *) vectoro;
         for(i = 0 ; i < size ; i ++)
         {
            ivectoro[i] = ivectori[perm[i]];
         }
         break;
      }
      case 01:
      {
         HYPRE_Int *ivectori     = (HYPRE_Int *) vectori;
         HYPRE_Int *ivectoro     = (HYPRE_Int *) vectoro;
         for(i = 0 ; i < size ; i ++)
         {
            ivectoro[perm[i]] = ivectori[i];
         }
         break;
      }
      case 10:
      {
         HYPRE_Real *dvectori     = (HYPRE_Real *) vectori;
         HYPRE_Real *dvectoro     = (HYPRE_Real *) vectoro;
         for(i = 0 ; i < size ; i ++)
         {
            dvectoro[i] = dvectori[perm[i]];
         }
         break;
      }
      case 11:
      {
         HYPRE_Real *dvectori     = (HYPRE_Real *) vectori;
         HYPRE_Real *dvectoro     = (HYPRE_Real *) vectoro;
         for(i = 0 ; i < size ; i ++)
         {
            dvectoro[perm[i]] = dvectori[i];
         }
         break;
      }
      default:
      {
         printf("Error option in ILUSeqVectorPerm");
         hypre_assert(1==0);
      }
   }
   return hypre_error_flag;
}

/* Incomplete LU solve (GPU)
 * L, D and U factors only have local scope (no off-diagonal processor terms)
 * so apart from the residual calculation (which uses A), the solves with the 
 * L and U factors are local.
*/

HYPRE_Int
hypre_ILUSolveCusparseLU(hypre_ParCSRMatrix *A, cusparseMatDescr_t matL_des, cusparseMatDescr_t matU_des,
                  csrsv2Info_t matL_info, csrsv2Info_t matU_info, hypre_CSRMatrix *matLU_d,
                  cusparseSolvePolicy_t ilu_solve_policy, void *ilu_solve_buffer,
                  hypre_ParVector *f,  hypre_ParVector *u, HYPRE_Int *perm, 
                  HYPRE_Int n, hypre_ParVector *ftemp, hypre_ParVector *utemp)
{
   
   /* Only solve when we have stuffs to be solved */
   if(n == 0)
   {
      return hypre_error_flag;
   }
   
   /* ILU data */
   HYPRE_Real              *LU_data             = hypre_CSRMatrixData(matLU_d);
   HYPRE_Int               *LU_i                = hypre_CSRMatrixI(matLU_d);
   HYPRE_Int               *LU_j                = hypre_CSRMatrixJ(matLU_d);
   HYPRE_Int               nnz                  = LU_i[n];  
   
   hypre_Vector            *utemp_local         = hypre_ParVectorLocalVector(utemp);
   HYPRE_Real              *utemp_data          = hypre_VectorData(utemp_local);
   
   hypre_Vector            *ftemp_local         = hypre_ParVectorLocalVector(ftemp);
   HYPRE_Real              *ftemp_data          = hypre_VectorData(ftemp_local);      

   HYPRE_Real              alpha;
   HYPRE_Real              beta;   
   //HYPRE_Int               i, j, k1, k2;
   
   HYPRE_Int               isDoublePrecision    = sizeof(HYPRE_Complex) == sizeof(hypre_double);
   HYPRE_Int               isSinglePrecision    = sizeof(HYPRE_Complex) == sizeof(hypre_double) / 2;
   
   hypre_assert(isDoublePrecision || isSinglePrecision);
   
   /* begin */
   alpha = -1.0;
   beta = 1.0;
   
   cusparseHandle_t handle = hypre_HandleCusparseHandle(hypre_handle);
   
   /* Initialize Utemp to zero. 
    * This is necessary for correctness, when we use optimized 
    * vector operations in the case where sizeof(L, D or U) < sizeof(A)
   */
   //hypre_ParVectorSetConstantValues( utemp, 0.);
   /* compute residual */
   hypre_ParCSRMatrixMatvecOutOfPlace(alpha, A, u, beta, f, ftemp);
   
   /* apply permutation */
   HYPRE_THRUST_CALL(gather, perm, perm + n, ftemp_data, utemp_data);
   
   if(isDoublePrecision)
   {
      /* L solve - Forward solve */
      HYPRE_CUSPARSE_CALL(cusparseDcsrsv2_solve(handle, CUSPARSE_OPERATION_NON_TRANSPOSE, 
                                                n, nnz, (double *) &beta, matL_des,
                                                (double *) LU_data, LU_i, LU_j, matL_info, 
                                                (double *) utemp_data, (double *) ftemp_data, ilu_solve_policy, ilu_solve_buffer));
      
      /* U solve - Backward substitution */
      HYPRE_CUSPARSE_CALL(cusparseDcsrsv2_solve(handle, CUSPARSE_OPERATION_NON_TRANSPOSE, 
                                                n, nnz, (double *) &beta, matU_des,
                                                (double *) LU_data, LU_i, LU_j, matU_info, 
                                                (double *) ftemp_data, (double *) utemp_data, ilu_solve_policy, ilu_solve_buffer));
   }
   else if(isSinglePrecision)
   {
      /* L solve - Forward solve */
      HYPRE_CUSPARSE_CALL(cusparseScsrsv2_solve(handle, CUSPARSE_OPERATION_NON_TRANSPOSE, 
                                                n, nnz, (float *) &beta, matL_des,
                                                (float *) LU_data, LU_i, LU_j, matL_info, 
                                                (float *) utemp_data, (float *) ftemp_data, ilu_solve_policy, ilu_solve_buffer));
      
      /* U solve - Backward substitution */
      HYPRE_CUSPARSE_CALL(cusparseScsrsv2_solve(handle, CUSPARSE_OPERATION_NON_TRANSPOSE, 
                                                n, nnz, (float *) &beta, matU_des,
                                                (float *) LU_data, LU_i, LU_j, matU_info, 
                                                (float *) ftemp_data, (float *) utemp_data, ilu_solve_policy, ilu_solve_buffer));
   }
   
   /* apply reverse permutation */
   HYPRE_THRUST_CALL(scatter,utemp_data, utemp_data + n, perm, ftemp_data);
   /* Update solution */
   hypre_ParVectorAxpy(beta, ftemp, u);
   
   
   return hypre_error_flag;
}

/* Schur Complement solve with GMRES on schur complement
 * ParCSRMatrix S is already built in ilu data sturcture, here directly use S
 * L, D and U factors only have local scope (no off-diagonal processor terms)
 * so apart from the residual calculation (which uses A), the solves with the 
 * L and U factors are local.
 * S is the global Schur complement
 * schur_solver is a GMRES solver
 * schur_precond is the ILU preconditioner for GMRES
 * rhs and x are helper vector for solving Schur system
*/

HYPRE_Int
hypre_ILUSolveCusparseSchurGMRES(hypre_ParCSRMatrix *A, hypre_ParVector *f,
                                 hypre_ParVector *u, HYPRE_Int *perm,
                                 HYPRE_Int nLU, hypre_ParCSRMatrix *S,
                                 hypre_ParVector *ftemp, hypre_ParVector *utemp,
                                 HYPRE_Solver schur_solver, HYPRE_Solver schur_precond,
                                 hypre_ParVector *rhs, hypre_ParVector *x, HYPRE_Int *u_end,
                                 cusparseMatDescr_t matL_des, cusparseMatDescr_t matU_des,
                                 csrsv2Info_t matBL_info, csrsv2Info_t matBU_info, csrsv2Info_t matSL_info, csrsv2Info_t matSU_info,
                                 hypre_CSRMatrix *matBLU_d, hypre_CSRMatrix *matE_d, hypre_CSRMatrix *matF_d, 
                                 cusparseSolvePolicy_t ilu_solve_policy, void *ilu_solve_buffer)
{
   /* If we don't have S block, just do one L solve and one U solve */
   if(!S)
   {
      /* Just call BJ cusparse and return */
      return hypre_ILUSolveCusparseLU(A, matL_des, matU_des, matBL_info, matBU_info, matBLU_d, ilu_solve_policy, 
                                       ilu_solve_buffer, f, u, perm, nLU, ftemp, utemp);
   }
   
   /* data objects for communication */
//   MPI_Comm          comm = hypre_ParCSRMatrixComm(A);
   
   /* data objects for temp vector */
   hypre_Vector      *utemp_local = hypre_ParVectorLocalVector(utemp);
   HYPRE_Real        *utemp_data  = hypre_VectorData(utemp_local);
   hypre_Vector      *ftemp_local = hypre_ParVectorLocalVector(ftemp);
   HYPRE_Real        *ftemp_data  = hypre_VectorData(ftemp_local);  
   hypre_Vector      *rhs_local   = hypre_ParVectorLocalVector(rhs);
   HYPRE_Real        *rhs_data    = hypre_VectorData(rhs_local);
   hypre_Vector      *x_local     = hypre_ParVectorLocalVector(x);
   HYPRE_Real        *x_data      = hypre_VectorData(x_local);

   HYPRE_Real        alpha;
   HYPRE_Real        beta;  
   //HYPRE_Real        gamma; 
   //HYPRE_Int         i, j, k1, k2, col;
   
   /* problem size */
   HYPRE_Int         *BLU_i      = NULL;
   HYPRE_Int         *BLU_j      = NULL;
   HYPRE_Real        *BLU_data   = NULL;
   HYPRE_Int         BLU_nnz     = 0;
   hypre_CSRMatrix   *matSLU_d   = hypre_ParCSRMatrixDiag(S);
   HYPRE_Int         *SLU_i      = hypre_CSRMatrixI(matSLU_d);
   HYPRE_Int         *SLU_j      = hypre_CSRMatrixJ(matSLU_d);
   HYPRE_Real        *SLU_data   = hypre_CSRMatrixData(matSLU_d);
   HYPRE_Int         m           = hypre_CSRMatrixNumRows(matSLU_d);
   HYPRE_Int         n           = nLU + m;
   HYPRE_Int         SLU_nnz     = SLU_i[m];
   
   hypre_Vector *ftemp_upper           = hypre_SeqVectorCreate(nLU);
   hypre_Vector *utemp_lower           = hypre_SeqVectorCreate(m);
   hypre_VectorOwnsData(ftemp_upper)   = 0;
   hypre_VectorOwnsData(utemp_lower)   = 0;
   hypre_VectorData(ftemp_upper)       = ftemp_data;
   hypre_VectorData(utemp_lower)       = utemp_data + nLU;
   hypre_SeqVectorInitialize(ftemp_upper);
   hypre_SeqVectorInitialize(utemp_lower);
   
   if( nLU > 0)
   {
      BLU_i                      = hypre_CSRMatrixI(matBLU_d);
      BLU_j                      = hypre_CSRMatrixJ(matBLU_d);
      BLU_data                   = hypre_CSRMatrixData(matBLU_d);
      BLU_nnz                    = BLU_i[nLU];
   }
   
   /* begin */
   beta = 1.0;
   alpha = -1.0;
   //gamma = 0.0;
   
   HYPRE_Int               isDoublePrecision    = sizeof(HYPRE_Complex) == sizeof(hypre_double);
   HYPRE_Int               isSinglePrecision    = sizeof(HYPRE_Complex) == sizeof(hypre_double) / 2;
   
   hypre_assert(isDoublePrecision || isSinglePrecision);
   
   cusparseHandle_t handle = hypre_HandleCusparseHandle(hypre_handle);
   cusparseMatDescr_t descr = hypre_HandleCusparseMatDescr(hypre_handle);
   
   /* compute residual */
   hypre_ParCSRMatrixMatvecOutOfPlace(alpha, A, u, beta, f, ftemp);
   
   /* 1st need to solve LBi*xi = fi  
    * L solve, solve xi put in u_temp upper 
    */
    
   /* apply permutation before we can start our solve */
   HYPRE_THRUST_CALL(gather, perm, perm + n, ftemp_data, utemp_data);
   
   if(nLU > 0)
   {
      
      /* This solve won't touch data in utemp, thus, gi is still in utemp_lower */
      if(isDoublePrecision)
      {
         /* L solve - Forward solve */
         HYPRE_CUSPARSE_CALL(cusparseDcsrsv2_solve(handle, CUSPARSE_OPERATION_NON_TRANSPOSE, 
                                                   nLU, BLU_nnz, (double *) &beta, matL_des,
                                                   (double *) BLU_data, BLU_i, BLU_j, matBL_info, 
                                                   (double *) utemp_data, (double *) ftemp_data, ilu_solve_policy, ilu_solve_buffer));
      }
      else if(isSinglePrecision)
      {
         /* L solve - Forward solve */
         HYPRE_CUSPARSE_CALL(cusparseScsrsv2_solve(handle, CUSPARSE_OPERATION_NON_TRANSPOSE, 
                                                   nLU, BLU_nnz, (float *) &beta, matL_des,
                                                   (float *) BLU_data, BLU_i, BLU_j, matBL_info, 
                                                   (float *) utemp_data, (float *) ftemp_data, ilu_solve_policy, ilu_solve_buffer));
      }
   
      /* 2nd need to compute g'i = gi - Ei*UBi^{-1}*xi
       * Ei*UBi^{-1} is exactly the matE_d here
       * Now:  LBi^{-1}f_i is in ftemp_upper
       *       gi' is in utemp_lower
       */
      
      hypre_CSRMatrixMatvec(alpha, matE_d, ftemp_upper, beta, utemp_lower);
      
   }
   
   /* 3rd need to solve global Schur Complement M^{-1}Sy = M^{-1}g'
    * for now only solve the local system
    * solve y put in u_temp lower
    * only solve whe S is not NULL 
    */
   
   /* setup vectors for solve 
    * rhs = M^{-1}g'
    */
   
   if(m > 0)
   {
      if(isDoublePrecision)
      {
         /* L solve */
         HYPRE_CUSPARSE_CALL(cusparseDcsrsv2_solve(handle, CUSPARSE_OPERATION_NON_TRANSPOSE, 
                                                   m, SLU_nnz, (double *) &beta, matL_des,
                                                   (double *) SLU_data, SLU_i, SLU_j, matSL_info, 
                                                   (double *) utemp_data + nLU, (double *) ftemp_data + nLU, ilu_solve_policy, ilu_solve_buffer));
                                                   
         /* U solve */
         HYPRE_CUSPARSE_CALL(cusparseDcsrsv2_solve(handle, CUSPARSE_OPERATION_NON_TRANSPOSE, 
                                                   m, SLU_nnz, (double *) &beta, matU_des,
                                                   (double *) SLU_data, SLU_i, SLU_j, matSU_info, 
                                                   (double *) ftemp_data + nLU, (double *) rhs_data, ilu_solve_policy, ilu_solve_buffer));
      }
      else if(isSinglePrecision)
      {
         /* L solve */
         HYPRE_CUSPARSE_CALL(cusparseScsrsv2_solve(handle, CUSPARSE_OPERATION_NON_TRANSPOSE, 
                                                   m, SLU_nnz, (float *) &beta, matL_des,
                                                   (float *) SLU_data, SLU_i, SLU_j, matSL_info, 
                                                   (float *) utemp_data + nLU, (float *) ftemp_data + nLU, ilu_solve_policy, ilu_solve_buffer));
         
         /* U solve */
         HYPRE_CUSPARSE_CALL(cusparseScsrsv2_solve(handle, CUSPARSE_OPERATION_NON_TRANSPOSE, 
                                                   m, SLU_nnz, (float *) &beta, matU_des,
                                                   (float *) SLU_data, SLU_i, SLU_j, matSU_info, 
                                                   (float *) ftemp_data + nLU, (float *) rhs_data, ilu_solve_policy, ilu_solve_buffer));
      }
   }
   
   
   /* solve */
   HYPRE_GMRESSolve(schur_solver,(HYPRE_Matrix)schur_precond,(HYPRE_Vector)rhs,(HYPRE_Vector)x);
   
   /* 4th need to compute zi = xi - LBi^-1*yi
    * put zi in f_temp upper 
    * only do this computation when nLU < n
    * U is unsorted, search is expensive when unnecessary
    */
   
   if(nLU > 0)
   {
      hypre_CSRMatrixMatvec(alpha, matF_d, x_local, beta, ftemp_upper);
      
      /* 5th need to solve UBi*ui = zi */
      /* put result in u_temp upper */
      
      if(isDoublePrecision)
      {
         /* L solve - Forward solve */
         HYPRE_CUSPARSE_CALL(cusparseDcsrsv2_solve(handle, CUSPARSE_OPERATION_NON_TRANSPOSE, 
                                                   nLU, BLU_nnz, (double *) &beta, matU_des,
                                                   (double *) BLU_data, BLU_i, BLU_j, matBU_info, 
                                                   (double *) ftemp_data, (double *) utemp_data, ilu_solve_policy, ilu_solve_buffer));
      }
      else if(isSinglePrecision)
      {
         /* L solve - Forward solve */
         HYPRE_CUSPARSE_CALL(cusparseScsrsv2_solve(handle, CUSPARSE_OPERATION_NON_TRANSPOSE, 
                                                   nLU, BLU_nnz, (float *) &beta, matU_des,
                                                   (float *) BLU_data, BLU_i, BLU_j, matBU_info, 
                                                   (float *) ftemp_data, (float *) utemp_data, ilu_solve_policy, ilu_solve_buffer));
      }
   }
   
   /* copy lower part solution into u_temp as well */
   hypre_TMemcpy(utemp_data + nLU, x_data, HYPRE_Real, m, HYPRE_MEMORY_SHARED, HYPRE_MEMORY_SHARED);
   
   /* perm back */
   HYPRE_THRUST_CALL(scatter,utemp_data, utemp_data + n, perm, ftemp_data);
   
   /* done, now everything are in u_temp, update solution */
   hypre_ParVectorAxpy(beta, ftemp, u);
        
   hypre_SeqVectorDestroy(ftemp_upper);
   hypre_SeqVectorDestroy(utemp_lower);
   
   return hypre_error_flag;
}

#endif

/* solve functions for NSH */

/*--------------------------------------------------------------------
 * hypre_NSHSolve
 *--------------------------------------------------------------------*/
HYPRE_Int
hypre_NSHSolve( void               *nsh_vdata,
                  hypre_ParCSRMatrix *A,
                  hypre_ParVector    *f,
                  hypre_ParVector    *u )
{
   MPI_Comm             comm = hypre_ParCSRMatrixComm(A);
//   HYPRE_Int            i;
   
   hypre_ParNSHData     *nsh_data = (hypre_ParNSHData*) nsh_vdata;
   
   /* get matrices */
   hypre_ParCSRMatrix   *matA = hypre_ParNSHDataMatA(nsh_data);
   hypre_ParCSRMatrix   *matM = hypre_ParNSHDataMatM(nsh_data);
   
   HYPRE_Int            iter, num_procs,  my_id;

   hypre_ParVector      *F_array = hypre_ParNSHDataF(nsh_data);
   hypre_ParVector      *U_array = hypre_ParNSHDataU(nsh_data);

   /* get settings */
   HYPRE_Real           tol = hypre_ParNSHDataTol(nsh_data);
   HYPRE_Int            logging = hypre_ParNSHDataLogging(nsh_data);
   HYPRE_Int            print_level = hypre_ParNSHDataPrintLevel(nsh_data);
   HYPRE_Int            max_iter = hypre_ParNSHDataMaxIter(nsh_data);
   HYPRE_Real           *norms = hypre_ParNSHDataRelResNorms(nsh_data);
   hypre_ParVector      *Ftemp = hypre_ParNSHDataFTemp(nsh_data);
   hypre_ParVector      *Utemp = hypre_ParNSHDataUTemp(nsh_data);
   hypre_ParVector      *residual;

   HYPRE_Real           alpha = -1.0;
   HYPRE_Real           beta = 1.0;
   HYPRE_Real           conv_factor = 0.0;
   HYPRE_Real           resnorm = 1.0;
   HYPRE_Real           init_resnorm = 0.0;
   HYPRE_Real           rel_resnorm;
   HYPRE_Real           rhs_norm = 0.0;
   HYPRE_Real           old_resnorm;
   HYPRE_Real           ieee_check = 0.;
   HYPRE_Real           operat_cmplxty = hypre_ParNSHDataOperatorComplexity(nsh_data);

   HYPRE_Int            Solve_err_flag;
   
   /* problem size */
//   HYPRE_Int            n = hypre_CSRMatrixNumRows(hypre_ParCSRMatrixDiag(A));
   
   /* begin */
   if(logging > 1)
   {
      residual = hypre_ParNSHDataResidual(nsh_data);
   }

   hypre_ParNSHDataNumIterations(nsh_data) = 0;

   hypre_MPI_Comm_size(comm, &num_procs);
   hypre_MPI_Comm_rank(comm,&my_id);

   /*-----------------------------------------------------------------------
    *    Write the solver parameters
    *-----------------------------------------------------------------------*/
   if (my_id == 0 && print_level > 1)
   {
      hypre_NSHWriteSolverParams(nsh_data);
   }

   /*-----------------------------------------------------------------------
    *    Initialize the solver error flag
    *-----------------------------------------------------------------------*/

   Solve_err_flag = 0;   
   /*-----------------------------------------------------------------------
    *     write some initial info
    *-----------------------------------------------------------------------*/

   if (my_id == 0 && print_level > 1 && tol > 0.)
   {
      hypre_printf("\n\n Newton–Schulz–Hotelling SOLVER SOLUTION INFO:\n");
   }


   /*-----------------------------------------------------------------------
    *    Compute initial residual and print
    *-----------------------------------------------------------------------*/
   if (print_level > 1 || logging > 1 || tol > 0.)
   {
      if ( logging > 1 ) 
      {
         hypre_ParVectorCopy(f, residual );
         if (tol > 0.0)
         {
            hypre_ParCSRMatrixMatvec(alpha, A, u, beta, residual );
         }
         resnorm = sqrt(hypre_ParVectorInnerProd( residual, residual ));
      }
      else 
      {
         hypre_ParVectorCopy(f, Ftemp);
         if (tol > 0.0)
         {
            hypre_ParCSRMatrixMatvec(alpha, A, u, beta, Ftemp);
         }
         resnorm = sqrt(hypre_ParVectorInnerProd(Ftemp, Ftemp));
      }

      /* Since it is does not diminish performance, attempt to return an error flag
         and notify users when they supply bad input. */
      if (resnorm != 0.) 
      {
         ieee_check = resnorm/resnorm; /* INF -> NaN conversion */
      }
      if (ieee_check != ieee_check)
      {
         /* ...INFs or NaNs in input can make ieee_check a NaN.  This test
           for ieee_check self-equality works on all IEEE-compliant compilers/
           machines, c.f. page 8 of "Lecture Notes on the Status of IEEE 754"
           by W. Kahan, May 31, 1996.  Currently (July 2002) this paper may be
           found at http://HTTP.CS.Berkeley.EDU/~wkahan/ieee754status/IEEE754.PDF */
         if (print_level > 0)
         {
            hypre_printf("\n\nERROR detected by Hypre ...  BEGIN\n");
            hypre_printf("ERROR -- hypre_NSHSolve: INFs and/or NaNs detected in input.\n");
            hypre_printf("User probably placed non-numerics in supplied A, x_0, or b.\n");
            hypre_printf("ERROR detected by Hypre ...  END\n\n\n");
         }
         hypre_error(HYPRE_ERROR_GENERIC);
         return hypre_error_flag;
      }

      init_resnorm = resnorm;
      rhs_norm = sqrt(hypre_ParVectorInnerProd(f, f));
      if (rhs_norm)
      {
         rel_resnorm = init_resnorm / rhs_norm;
      }
      else
      {
         /* rhs is zero, return a zero solution */
         hypre_ParVectorSetConstantValues(U_array, 0.0);
         if(logging > 0)
         {
            rel_resnorm = 0.0;
            hypre_ParNSHDataFinalRelResidualNorm(nsh_data) = rel_resnorm;
         }
         return hypre_error_flag;
      }
   }
   else
   {
      rel_resnorm = 1.;
   }

   if (my_id == 0 && print_level > 1)
   {
      hypre_printf("                                            relative\n");
      hypre_printf("               residual        factor       residual\n");
      hypre_printf("               --------        ------       --------\n");
      hypre_printf("    Initial    %e                 %e\n",init_resnorm,
              rel_resnorm);
   }

   matA = A;
   U_array = u;
   F_array = f;

   /************** Main Solver Loop - always do 1 iteration ************/
   iter = 0;
   
   while ((rel_resnorm >= tol || iter < 1)
          && iter < max_iter)
   {

      /* Do one solve on e = Mr */
      hypre_NSHSolveInverse(matA, f, u, matM, Utemp, Ftemp);

      /*---------------------------------------------------------------
       *    Compute residual and residual norm
       *----------------------------------------------------------------*/

      if (print_level > 1 || logging > 1 || tol > 0.)
      {
        old_resnorm = resnorm;

        if ( logging > 1 ) {
           hypre_ParVectorCopy(F_array, residual);
           hypre_ParCSRMatrixMatvec(alpha, matA, U_array, beta, residual );
           resnorm = sqrt(hypre_ParVectorInnerProd( residual, residual ));
        }
        else {
           hypre_ParVectorCopy(F_array, Ftemp);
           hypre_ParCSRMatrixMatvec(alpha, matA, U_array, beta, Ftemp);
           resnorm = sqrt(hypre_ParVectorInnerProd(Ftemp, Ftemp));
        }

        if (old_resnorm) conv_factor = resnorm / old_resnorm;
        else conv_factor = resnorm;
        if (rhs_norm)
        {
           rel_resnorm = resnorm / rhs_norm;
        }
        else
        {
           rel_resnorm = resnorm;
        }

        norms[iter] = rel_resnorm;
      }

      ++iter;
      hypre_ParNSHDataNumIterations(nsh_data) = iter;
      hypre_ParNSHDataFinalRelResidualNorm(nsh_data) = rel_resnorm;

      if (my_id == 0 && print_level > 1)
      {
         hypre_printf("    NSHSolve %2d   %e    %f     %e \n", iter,
                 resnorm, conv_factor, rel_resnorm);
      }
   }

   /* check convergence within max_iter */
   if (iter == max_iter && tol > 0.)
   {
      Solve_err_flag = 1;
      hypre_error(HYPRE_ERROR_CONV);
   }

   /*-----------------------------------------------------------------------
    *    Print closing statistics
    *    Add operator and grid complexity stats
    *-----------------------------------------------------------------------*/

   if (iter > 0 && init_resnorm)
   {
      conv_factor = pow((resnorm/init_resnorm),(1.0/(HYPRE_Real) iter));
   }
   else
   {
      conv_factor = 1.;
   }

   if (print_level > 1)
   {
      /*** compute operator and grid complexity (fill factor) here ?? ***/
      if (my_id == 0)
      {
         if (Solve_err_flag == 1)
         {
            hypre_printf("\n\n==============================================");
            hypre_printf("\n NOTE: Convergence tolerance was not achieved\n");
            hypre_printf("      within the allowed %d iterations\n",max_iter);
            hypre_printf("==============================================");
         }
         hypre_printf("\n\n Average Convergence Factor = %f \n",conv_factor);
         hypre_printf("                operator = %f\n",operat_cmplxty);
      }
   }
   return hypre_error_flag;
}

/* NSH solve
 * Simply a matvec on residual with approximate inverse
 * A: original matrix
 * f: rhs
 * u: solution
 * M: approximate inverse
 * ftemp, utemp: working vectors
*/
HYPRE_Int
hypre_NSHSolveInverse(hypre_ParCSRMatrix *A, hypre_ParVector *f,
                        hypre_ParVector *u, hypre_ParCSRMatrix *M,
                        hypre_ParVector *ftemp, hypre_ParVector *utemp)
{
   HYPRE_Real      alpha;
   HYPRE_Real      beta;

   /* begin */
   alpha = -1.0;
   beta = 1.0;
   /* r = f-Au */
   hypre_ParCSRMatrixMatvecOutOfPlace(alpha, A, u, beta, f, ftemp);
   /* e = Mr */
//hypre_ParCSRMatrixDiag(M) = hypre_ParCSRMatrixDiag(A);
//hypre_ParCSRMatrixOffd(M) = hypre_ParCSRMatrixOffd(A);
   hypre_ParCSRMatrixMatvec(1.0, M, ftemp, 0.0, utemp);
   /* u = u + e */
   hypre_ParVectorAxpy(beta, utemp, u);
   return hypre_error_flag;
}
