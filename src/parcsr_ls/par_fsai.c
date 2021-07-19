/******************************************************************************
 * Copyright 1998-2019 Lawrence Livermore National Security, LLC and other
 * HYPRE Project Developers. See the top-level COPYRIGHT file for details.
 *
 * SPDX-License-Identifier: (Apache-2.0 OR MIT)
 *******************************************************************************/

#include "_hypre_parcsr_ls.h"
#include "par_fsai.h"

/******************************************************************************
 * HYPRE_FSAICreate
 ******************************************************************************/

void *
hypre_FSAICreate()
{

   hypre_ParFSAIData    *fsai_data;
   HYPRE_Int		      *comm_info;

   /* setup params */
   HYPRE_Int            max_steps;
   HYPRE_Int            max_step_size;
   HYPRE_Real           kap_tolerance;

   /* solver params */
   HYPRE_Int            max_iterations;
   HYPRE_Int            num_iterations;
   HYPRE_Real           rel_resid_norm;
   HYPRE_Real           tolerance;
   HYPRE_Real           omega;

   /* log info */
   HYPRE_Int            logging;

   /* output params */
   HYPRE_Int            print_level;
   HYPRE_Int            debug_flag;
   char                 log_file_name[256];

   /*-----------------------------------------------------------------------
    * Setup default values for parameters
    *-----------------------------------------------------------------------*/

   /* setup params */
   max_steps = 10;
   max_step_size = 3;
   kap_tolerance = 1.0e-3;

   /* solver params */
   max_iterations = 20;
   tolerance = 1.0e-6;

   /* log info */
   logging = 0;
   num_iterations = 0;

   /* output params */
   print_level = 0;
   debug_flag = 0;
   hypre_sprintf(log_file_name, "%s", "fsai.out.log");

   #if defined(HYPRE_USING_GPU)
   #endif

   HYPRE_ANNOTATE_FUNC_BEGIN;

   /*-----------------------------------------------------------------------
    * Create the hypre_ParFSAIData structure and return
    *-----------------------------------------------------------------------*/

   fsai_data = hypre_CTAlloc(hypre_ParFSAIData, 1, HYPRE_MEMORY_HOST);

   hypre_ParFSAIDataMemoryLocation(fsai_data)   = HYPRE_MEMORY_UNDEFINED;

   hypre_ParFSAIDataGmat(fsai_data)             = NULL;
   hypre_ParFSAIDataResidual(fsai_data)         = NULL;
   hypre_ParFSAIDataCommInfo(fsai_data)         = NULL;
   hypre_ParFSAIDataNewComm(fsai_data)          = hypre_MPI_COMM_NULL;

   hypre_FSAISetMaxSteps(fsai_data, max_steps);
   hypre_FSAISetMaxStepSize(fsai_data, max_step_size);
   hypre_FSAISetKapTolerance(fsai_data, kap_tolerance);

   hypre_FSAISetMaxIterations(fsai_data, max_iterations);
   hypre_FSAISetTolerance(fsai_data, tolerance);
   hypre_FSAISetOmega(fsai_data, omega);

   hypre_FSAISetLogging(fsai_data, logging);
   hypre_FSAISetNumIterations(fsai_data, num_iterations);

   hypre_FSAISetPrintLevel(fsai_data, print_level);
   hypre_FSAISetPrintFileName(fsai_data, log_file_name);
   hypre_FSAISetDebugFlag(fsai_data, debug_flag);

   HYPRE_ANNOTATE_FUNC_END;

   return (void *) fsai_data;

}


/******************************************************************************
 * HYPRE_FSAIDestroy
 ******************************************************************************/

HYPRE_Int
hypre_FSAIDestroy( void *data )
{
   hypre_ParFSAIData *fsai_data = (hypre_ParFSAIData*)data;
   MPI_Comm new_comm = hypre_ParFSAIDataNewComm(fsai_data);

   HYPRE_ANNOTATE_FUNC_BEGIN;

   if(hypre_ParFSAIDataCommInfo(fsai_data)) hypre_TFree(hypre_ParFSAIDataCommInfo(fsai_data), HYPRE_MEMORY_HOST);
   if(hypre_ParFSAIDataGmat(fsai_data))     hypre_ParCSRMatrixDestroy(hypre_ParFSAIDataGmat(fsai_data));
   if(hypre_ParFSAIDataGTmat(fsai_data))    hypre_ParCSRMatrixDestroy(hypre_ParFSAIDataGTmat(fsai_data));
   if(hypre_ParFSAIDataResidual(fsai_data)) HYPRE_ParVectorDestroy(hypre_ParFSAIDataResidual(fsai_data));

   if( new_comm != hypre_MPI_COMM_NULL )
      hypre_MPI_Comm_free(&new_comm);

   hypre_TFree(fsai_data, HYPRE_MEMORY_HOST);

   HYPRE_ANNOTATE_FUNC_END;

   return hypre_error_flag;
}

/******************************************************************************
 * Routines to SET the setup phase parameters
 ******************************************************************************/


HYPRE_Int
hypre_FSAISetMaxSteps( void *data,
                       HYPRE_Int max_steps )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   if (max_steps < 0)
   {
      hypre_error_in_arg(2);
      return hypre_error_flag;
   }

   hypre_ParFSAIDataMaxSteps(fsai_data) = max_steps;

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAISetMaxStepSize( void *data,
                          HYPRE_Int max_step_size )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   if (max_step_size < 0)
   {
      hypre_error_in_arg(2);
      return hypre_error_flag;
   }

   hypre_ParFSAIDataMaxStepSize(fsai_data) = max_step_size;

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAISetKapTolerance( void *data,
                           HYPRE_Real kap_tolerance )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   if (kap_tolerance < 0)
   {
      hypre_error_in_arg(2);
      return hypre_error_flag;
   }

   hypre_ParFSAIDataKapTolerance(fsai_data) = kap_tolerance;

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAISetMaxIterations( void *data,
                            HYPRE_Int max_iterations )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   if (max_iterations < 0)
   {
      hypre_error_in_arg(2);
      return hypre_error_flag;
   }

   hypre_ParFSAIDataMaxIterations(fsai_data) = max_iterations;

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAISetTolerance( void *data,
                        HYPRE_Real tolerance )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   if (tolerance < 0)
   {
      hypre_error_in_arg(2);
      return hypre_error_flag;
   }

   hypre_ParFSAIDataTolerance(fsai_data) = tolerance;

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAISetOmega( void *data,
                    HYPRE_Real omega )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   if (omega < 0)
   {
      hypre_error_in_arg(2);
      return hypre_error_flag;
   }

   hypre_ParFSAIDataOmega(fsai_data) = omega;

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAISetLogging( void *data,
                      HYPRE_Int logging )
{
/*   This function should be called before Setup.  Logging changes
 *    may require allocation or freeing of arrays, which is presently
 *    only done there.
 *    It may be possible to support logging changes at other times,
 *    but there is little need.
 */
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   if (logging < 0)
   {
      hypre_error_in_arg(2);
      return hypre_error_flag;
   }

   hypre_ParFSAIDataLogging(fsai_data) = logging;

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAISetNumIterations( void *data,
                            HYPRE_Int num_iterations )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   if (num_iterations < 0)
   {
      hypre_error_in_arg(2);
      return hypre_error_flag;
   }

   hypre_ParFSAIDataNumIterations(fsai_data) = num_iterations;

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAISetPrintLevel( void *data,
                         HYPRE_Int print_level )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   if (print_level < 0)
   {
      hypre_error_in_arg(2);
      return hypre_error_flag;
   }

   hypre_ParFSAIDataPrintLevel(fsai_data) = print_level;

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAISetPrintFileName( void *data,
                            const char *print_file_name )
{
   hypre_ParFSAIData  *fsai_data =  (hypre_ParFSAIData*) data;
   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }
   if ( strlen(print_file_name) > 256 )
   {
      hypre_error_in_arg(2);
      return hypre_error_flag;
   }

   hypre_sprintf(hypre_ParFSAIDataLogFileName(fsai_data), "%s", print_file_name);

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAISetDebugFlag( void *data,
                        HYPRE_Int debug_flag )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   if (debug_flag < 0)
   {
      hypre_error_in_arg(2);
      return hypre_error_flag;
   }

   hypre_ParFSAIDataDebugFlag(fsai_data) = debug_flag;

   return hypre_error_flag;
}

/******************************************************************************
 * Routines to GET the setup phase parameters
 ******************************************************************************/

HYPRE_Int
hypre_FSAIGetMaxSteps( void *data,
                       HYPRE_Int *max_steps )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   *max_steps = hypre_ParFSAIDataMaxSteps(fsai_data);

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAIGetMaxStepSize( void *data,
                          HYPRE_Int *max_step_size )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   *max_step_size = hypre_ParFSAIDataMaxStepSize(fsai_data);

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAIGetKapTolerance( void *data,
                           HYPRE_Real *kap_tolerance )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   *kap_tolerance = hypre_ParFSAIDataKapTolerance(fsai_data);

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAIGetMaxIterations( void *data,
                            HYPRE_Int *max_iterations )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   *max_iterations = hypre_ParFSAIDataMaxIterations(fsai_data);

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAIGetTolerance( void *data,
                        HYPRE_Real *tolerance )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   *tolerance = hypre_ParFSAIDataTolerance(fsai_data);

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAIGetOmega( void *data,
                    HYPRE_Real *omega )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   *omega = hypre_ParFSAIDataOmega(fsai_data);

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAIGetLogging( void *data,
                      HYPRE_Int *logging )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   *logging = hypre_ParFSAIDataLogging(fsai_data);

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAIGetNumIterations( void *data,
                            HYPRE_Int *num_iterations )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   *num_iterations = hypre_ParFSAIDataNumIterations(fsai_data);

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAIGetPrintLevel( void *data,
                         HYPRE_Int *print_level )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   *print_level = hypre_ParFSAIDataPrintLevel(fsai_data);

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAIGetPrintFileName( void *data,
                            char **print_file_name )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }
   hypre_sprintf( *print_file_name, "%s", hypre_ParFSAIDataLogFileName(fsai_data) );

   return hypre_error_flag;
}

HYPRE_Int
hypre_FSAIGetDebugFlag( void *data,
                        HYPRE_Int *debug_flag )
{
   hypre_ParFSAIData  *fsai_data = (hypre_ParFSAIData*) data;

   if (!fsai_data)
   {
      hypre_error_in_arg(1);
      return hypre_error_flag;
   }

   *debug_flag = hypre_ParFSAIDataDebugFlag(fsai_data);

   return hypre_error_flag;
}
