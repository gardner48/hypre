/*BHEADER**********************************************************************
 * (c) 1998   The Regents of the University of California
 *
 * See the file COPYRIGHT_and_DISCLAIMER for a complete copyright
 * notice, contact person, and disclaimer.
 *
 * $Revision$
 *********************************************************************EHEADER*/

/******************************************************************************
 *
 * HYPRE_StructPCG interface
 *
 *****************************************************************************/
#include "headers.h"

#ifdef HYPRE_USE_PTHREADS
#include "box_pthreads.h"
#endif
/*--------------------------------------------------------------------------
 * HYPRE_ParCSRGMRESInitialize
 *--------------------------------------------------------------------------*/

int
HYPRE_ParCSRGMRESInitialize( MPI_Comm comm, HYPRE_Solver *solver )
{
   *solver = ( (HYPRE_Solver) hypre_GMRESInitialize( ) );

   return 0;
}

/*--------------------------------------------------------------------------
 * HYPRE_ParCSRGMRESFinalize
 *--------------------------------------------------------------------------*/

int 
HYPRE_ParCSRGMRESFinalize( HYPRE_Solver solver )
{
   return( hypre_GMRESFinalize( (void *) solver ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_ParCSRGMRESSetup
 *--------------------------------------------------------------------------*/

int 
HYPRE_ParCSRGMRESSetup( HYPRE_Solver solver,
                        HYPRE_ParCSRMatrix A,
                        HYPRE_ParVector b,
                        HYPRE_ParVector x      )
{
   return( hypre_GMRESSetup( (void *) solver,
                             (void *) A,
                             (void *) b,
                             (void *) x ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_ParCSRGMRESSolve
 *--------------------------------------------------------------------------*/

int 
HYPRE_ParCSRGMRESSolve( HYPRE_Solver solver,
                        HYPRE_ParCSRMatrix A,
                        HYPRE_ParVector b,
                        HYPRE_ParVector x      )
{
   return( hypre_GMRESSolve( (void *) solver,
                             (void *) A,
                             (void *) b,
                             (void *) x ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_ParCSRGMRESSetKDim
 *--------------------------------------------------------------------------*/

int
HYPRE_ParCSRGMRESSetKDim( HYPRE_Solver solver,
                          int             k_dim    )
{
   return( hypre_GMRESSetKDim( (void *) solver, k_dim ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_ParCSRGMRESSetTol
 *--------------------------------------------------------------------------*/

int
HYPRE_ParCSRGMRESSetTol( HYPRE_Solver solver,
                         double             tol    )
{
   return( hypre_GMRESSetTol( (void *) solver, tol ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_ParCSRGMRESSetMaxIter
 *--------------------------------------------------------------------------*/

int
HYPRE_ParCSRGMRESSetMaxIter( HYPRE_Solver solver,
                             int                max_iter )
{
   return( hypre_GMRESSetMaxIter( (void *) solver, max_iter ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_ParCSRGMRESSetPrecond
 *--------------------------------------------------------------------------*/

int
HYPRE_ParCSRGMRESSetPrecond( HYPRE_Solver  solver,
                             int               (*precond)(),
                             int               (*precond_setup)(),
                             void               *precond_data )
{
   return( hypre_GMRESSetPrecond( (void *) solver,
                                precond, precond_setup, precond_data ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_ParCSRGMRESSetLogging
 *--------------------------------------------------------------------------*/

int
HYPRE_ParCSRGMRESSetLogging( HYPRE_Solver solver,
                             int logging)
{
   return( hypre_GMRESSetLogging( (void *) solver, logging ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_ParCSRGMRESGetNumIterations
 *--------------------------------------------------------------------------*/

int
HYPRE_ParCSRGMRESGetNumIterations( HYPRE_Solver  solver,
                                   int                *num_iterations )
{
   return( hypre_GMRESGetNumIterations( (void *) solver, num_iterations ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_ParCSRGMRESGetFinalRelativeResidualNorm
 *--------------------------------------------------------------------------*/

int
HYPRE_ParCSRGMRESGetFinalRelativeResidualNorm( HYPRE_Solver  solver,
                                               double             *norm   )
{
   return( hypre_GMRESGetFinalRelativeResidualNorm( (void *) solver, norm ) );
}
