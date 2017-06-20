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
 * Header info for the BoxLoop
 *
 *****************************************************************************/

/*--------------------------------------------------------------------------
 * BoxLoop macros:
 *--------------------------------------------------------------------------*/

#ifndef HYPRE_NEWBOXLOOP_HEADER
#define HYPRE_NEWBOXLOOP_HEADER
extern "C++" {
#include <Kokkos_Core.hpp>
using namespace Kokkos;
}
#if defined( KOKKOS_HAVE_MPI )
#include <mpi.h>
#endif

typedef struct hypre_Boxloop_struct
{
   HYPRE_Int lsize0,lsize1,lsize2;
   HYPRE_Int strides0,strides1,strides2;
   HYPRE_Int bstart0,bstart1,bstart2;
   HYPRE_Int bsize0,bsize1,bsize2;
} hypre_Boxloop;

#if defined(HYPRE_MEMORY_GPU) || defined(HYPRE_USE_MANAGED)
#include <cuda.h>
#include <cuda_runtime.h>
#define AxCheckError(err) CheckError(err, __FUNCTION__, __LINE__)
void CheckError(cudaError_t const err, char const* const fun, const HYPRE_Int line)
{
   if (err)
   {
      printf("CUDA Error Code[%d]: %s\n%s() Line:%d\n", err, cudaGetErrorString(err), fun, line);
   }
}
#define BLOCKSIZE 256

#define hypre_fence()				\
   cudaError err = cudaGetLastError();		\
   if ( cudaSuccess != err ) {						\
     printf("\n ERROR hypre_newBoxLoop: %s in %s(%d) function %s\n",cudaGetErrorString(err),__FILE__,__LINE__,__FUNCTION__); \
   }									\
   AxCheckError(cudaDeviceSynchronize());
#elif defined(HYPRE_USE_OPENMP)
   #define hypre_fence() ;
#elif defined(HYPRE_USING_OPENMP_ACC)
   #define hypre_fence()  
#else
   #define hypre_fence();
#endif

#define hypre_newBoxLoopInit(ndim,loop_size)				\
   HYPRE_Int hypre__tot = 1;						\
   for (HYPRE_Int d = 0;d < ndim;d ++)					\
      hypre__tot *= loop_size[d];


#define hypre_BoxLoopIncK(k,box,hypre__i)				\
   HYPRE_Int hypre_boxD##k = 1;						\
   HYPRE_Int hypre__i = 0;							\
   hypre__i += (hypre_IndexD(local_idx, 0)*box.strides0 + box.bstart0) * hypre_boxD##k;	\
   hypre_boxD##k *= hypre_max(0, box.bsize0 + 1);			\
   hypre__i += (hypre_IndexD(local_idx, 1)*box.strides1 + box.bstart1) * hypre_boxD##k; \
   hypre_boxD##k *= hypre_max(0, box.bsize1 + 1);			\
   hypre__i += (hypre_IndexD(local_idx, 2)*box.strides2 + box.bstart2) * hypre_boxD##k; \
   hypre_boxD##k *= hypre_max(0, box.bsize2 + 1);		\

#define hypre_newBoxLoopDeclare(box)					\
  hypre_Index local_idx;						\
  HYPRE_Int idx_local = idx;						\
  hypre_IndexD(local_idx, 0)  = idx_local % box.lsize0;			\
  idx_local = idx_local / box.lsize0;					\
  hypre_IndexD(local_idx, 1)  = idx_local % box.lsize1;			\
  idx_local = idx_local / box.lsize1;					\
  hypre_IndexD(local_idx, 2)  = idx_local % box.lsize2;

#define hypre_BoxLoopDataDeclareK(k,ndim,loop_size,dbox,start,stride)	\
   hypre_Boxloop databox##k;     					\
   databox##k.lsize0 = loop_size[0];					\
   databox##k.strides0 = stride[0];					\
   databox##k.bstart0  = start[0] - dbox->imin[0];			\
   databox##k.bsize0   = dbox->imax[0]-dbox->imin[0];			\
   if (ndim > 1)							\
   {									\
      databox##k.lsize1 = loop_size[1];					\
      databox##k.strides1 = stride[1];				\
      databox##k.bstart1  = start[1] - dbox->imin[1];			\
      databox##k.bsize1   = dbox->imax[1]-dbox->imin[1];		\
   }									\
   else									\
   {									\
      databox##k.lsize1 = 1;						\
      databox##k.strides1 = 0;					\
      databox##k.bstart1  = 0;					\
      databox##k.bsize1   = 0;					\
   }									\
   if (ndim == 3)							\
   {									\
      databox##k.lsize2 = loop_size[2];					\
      databox##k.strides2 = stride[2];				\
      databox##k.bstart2  = start[2] - dbox->imin[2];		\
      databox##k.bsize2   = dbox->imax[2]-dbox->imin[2];		\
   }									\
   else									\
   {									\
      databox##k.lsize2 = 1;						\
      databox##k.strides2 = 0;					\
      databox##k.bstart2  = 0;					\
      databox##k.bsize2   = 0;				\
   }

#define hypre_newBoxLoop0Begin(ndim, loop_size)				\
{									\
   hypre_newBoxLoopInit(ndim,loop_size);					\
   Kokkos::parallel_for (hypre__tot, KOKKOS_LAMBDA (HYPRE_Int idx)	\
   {


#define hypre_newBoxLoop0End(i1)					\
   });									\
}


#define hypre_newBoxLoop1Begin(ndim, loop_size,				\
                               dbox1, start1, stride1, i1)	        \
{									\
   hypre_newBoxLoopInit(ndim,loop_size)					\
   hypre_BoxLoopDataDeclareK(1,ndim,loop_size,dbox1,start1,stride1);	\
   Kokkos::parallel_for (hypre__tot, KOKKOS_LAMBDA (HYPRE_Int idx)	\
   {									\
      hypre_newBoxLoopDeclare(databox1);				\
      hypre_BoxLoopIncK(1,databox1,i1);


#define hypre_newBoxLoop1End(i1)					\
   });									\
     hypre_fence();							\
 }


#define hypre_newBoxLoop2Begin(ndim, loop_size,				\
			       dbox1, start1, stride1, i1,		\
			       dbox2, start2, stride2, i2)		\
{									\
   hypre_newBoxLoopInit(ndim,loop_size);				\
   hypre_BoxLoopDataDeclareK(1,ndim,loop_size,dbox1,start1,stride1);	\
   hypre_BoxLoopDataDeclareK(2,ndim,loop_size,dbox2,start2,stride2);	\
   Kokkos::parallel_for (hypre__tot, KOKKOS_LAMBDA (HYPRE_Int idx)	\
   {									\
      hypre_newBoxLoopDeclare(databox1)					\
      hypre_BoxLoopIncK(1,databox1,i1);					\
      hypre_BoxLoopIncK(2,databox2,i2);

#define hypre_newBoxLoop2End(i1, i2)				\
   });								\
   hypre_fence();						\
}


#define hypre_newBoxLoop3Begin(ndim, loop_size,				\
			       dbox1, start1, stride1, i1,		\
			       dbox2, start2, stride2, i2,		\
			       dbox3, start3, stride3, i3)		\
{									\
   hypre_newBoxLoopInit(ndim,loop_size);				\
   hypre_BoxLoopDataDeclareK(1,ndim,loop_size,dbox1,start1,stride1);	\
   hypre_BoxLoopDataDeclareK(2,ndim,loop_size,dbox2,start2,stride2);	\
   hypre_BoxLoopDataDeclareK(3,ndim,loop_size,dbox3,start3,stride3);	\
   Kokkos::parallel_for (hypre__tot, KOKKOS_LAMBDA (HYPRE_Int idx)	\
   {						\
      hypre_newBoxLoopDeclare(databox1);		\
      hypre_BoxLoopIncK(1,databox1,i1);					\
      hypre_BoxLoopIncK(2,databox2,i2);					\
      hypre_BoxLoopIncK(3,databox3,i3);

#define hypre_newBoxLoop3End(i1, i2, i3)			\
   });								\
   hypre_fence();						\
}

#define hypre_newBoxLoop4Begin(ndim, loop_size,				\
			       dbox1, start1, stride1, i1,		\
			       dbox2, start2, stride2, i2,		\
			       dbox3, start3, stride3, i3,		\
			       dbox4, start4, stride4, i4)		\
{									\
   hypre_newBoxLoopInit(ndim,loop_size);				\
   hypre_BoxLoopDataDeclareK(1,ndim,loop_size,dbox1,start1,stride1);	\
   hypre_BoxLoopDataDeclareK(2,ndim,loop_size,dbox2,start2,stride2);	\
   hypre_BoxLoopDataDeclareK(3,ndim,loop_size,dbox3,start3,stride3);	\
   hypre_BoxLoopDataDeclareK(4,ndim,loop_size,dbox4,start4,stride4);	\
   Kokkos::parallel_for (hypre__tot, KOKKOS_LAMBDA (HYPRE_Int idx)	\
   {						\
      hypre_newBoxLoopDeclare(databox1);				\
      hypre_BoxLoopIncK(1,databox1,i1);					\
      hypre_BoxLoopIncK(2,databox2,i2);					\
      hypre_BoxLoopIncK(3,databox3,i3);					\
      hypre_BoxLoopIncK(4,databox4,i4);


#define hypre_newBoxLoop4End(i1, i2, i3, i4)			\
   });								\
   hypre_fence();						\
}

#define hypre_BasicBoxLoopDataDeclareK(k,ndim,loop_size,stride)		\
	hypre_Boxloop databox##k;					\
	databox##k.lsize0 = loop_size[0];				\
	databox##k.strides0 = stride[0];				\
	if (ndim > 1)							\
	{								\
	    databox##k.lsize1 = loop_size[1];				\
	    databox##k.strides1 = stride[1];				\
	}								\
	else						        	\
	{							       	\
		databox##k.lsize1 = 1;				       	\
		databox##k.strides1 = 0;		       		\
	}								\
	if (ndim == 3)							\
	{								\
	    databox##k.lsize2 = loop_size[2];				\
	    databox##k.strides2 = stride[2];				\
	}								\
	else								\
	{								\
	    databox##k.lsize2 = 1;					\
	    databox##k.strides2 = 0;					\
	}

#define zypre_newBasicBoxLoop2Begin(ndim, loop_size,			\
				    stride1, i1,			\
				    stride2, i2)			\
{    		       				                	\
   hypre_newBoxLoopInit(ndim,loop_size);				\
   hypre_BasicBoxLoopDataDeclareK(1,ndim,loop_size,stride1);		\
   hypre_BasicBoxLoopDataDeclareK(2,ndim,loop_size,stride2);		\
   Kokkos::parallel_for (hypre__tot, KOKKOS_LAMBDA (HYPRE_Int idx)	\
   {									\
      hypre_newBoxLoopDeclare(databox1);				\
      hypre_BoxLoopIncK(1,databox1,i1);					\
      hypre_BoxLoopIncK(2,databox2,i2);					\

#define hypre_newBoxLoop1ReductionBegin(ndim, loop_size,		\
					dbox1, start1, stride1, i1, sum) \
 {									\
     HYPRE_Real sum_tmp = sum;						\
     sum = 0;								\
     hypre_newBoxLoopInit(ndim,loop_size);					\
     hypre_BoxLoopDataDeclareK(1,ndim,loop_size,dbox1,start1,stride1);	\
     Kokkos::parallel_reduce (hypre__tot, KOKKOS_LAMBDA (HYPRE_Int idx,HYPRE_Real &sum) \
     {									\
       hypre_newBoxLoopDeclare(databox1);				\
       hypre_BoxLoopIncK(1,databox1,i1);				\



 #define hypre_newBoxLoop1ReductionEnd(i1, sum)				\
     },sum);								\
     hypre_fence();							\
     sum += sum_tmp;							\
 }

 #define hypre_newBoxLoop2ReductionBegin(ndim, loop_size,		\
					 dbox1, start1, stride1, i1,	\
					 dbox2, start2, stride2, i2, sum) \
 {									\
     HYPRE_Real sum_tmp = sum;						\
     sum = 0.0;								\
     hypre_newBoxLoopInit(ndim,loop_size);				\
     hypre_BoxLoopDataDeclareK(1,ndim,loop_size,dbox1,start1,stride1);	\
     hypre_BoxLoopDataDeclareK(2,ndim,loop_size,dbox2,start2,stride2);	\
     Kokkos::parallel_reduce (hypre__tot, KOKKOS_LAMBDA (HYPRE_Int idx,HYPRE_Real &sum) \
     {									\
	 hypre_newBoxLoopDeclare(databox1);				\
	 hypre_BoxLoopIncK(1,databox1,i1);				\
	 hypre_BoxLoopIncK(2,databox2,i2);				\

 #define hypre_newBoxLoop2ReductionEnd(i1, i2, sum)			\
     },sum);								\
     hypre_fence();							\
     sum +=sum_tmp;							\
 }

#define hypre_LoopBegin(size,idx)					\
{									\
   Kokkos::parallel_for(size, KOKKOS_LAMBDA (HYPRE_Int idx)		\
   {

#define hypre_LoopEnd()							\
   });									\
   hypre_fence();							\
}

extern "C++"
{
struct ColumnSums
{
  typedef HYPRE_Real value_type[];
  typedef View<HYPRE_Real**>::size_type size_type;
  size_type value_count;
  View<HYPRE_Real**> X_;
  ColumnSums(const View<HYPRE_Real**>& X):value_count(X.dimension_1()),X_(X){}
  KOKKOS_INLINE_FUNCTION void
  operator()(const size_type i,value_type sum) const
  {
    for (size_type j = 0;j < value_count;j++)
    {
       sum[j] += X_(i,j);
    }
  }
  KOKKOS_INLINE_FUNCTION void
  join (volatile value_type dst,volatile value_type src) const
  {
    for (size_type j= 0;j < value_count;j++)
    {
      dst[j] +=src[j];
    }
  }
  KOKKOS_INLINE_FUNCTION void init(value_type sum) const
  {
    for (size_type j= 0;j < value_count;j++)
    {
      sum[j] += 0.0;
    }
  }
};
}

#define hypre_newBoxLoopSetOneBlock() {}

#define hypre_newBoxLoopGetIndex(index)\
  index[0] = hypre_IndexD(local_idx, 0); index[1] = hypre_IndexD(local_idx, 1); index[2] = hypre_IndexD(local_idx, 2);
  
#define hypre_BoxLoopGetIndex    zypre_BoxLoopGetIndex
#define hypre_BoxLoopSetOneBlock hypre_newBoxLoopSetOneBlock
#define hypre_BoxLoopBlock()       0
#define hypre_BoxLoop0Begin      hypre_newBoxLoop0Begin
#define hypre_BoxLoop0For        hypre_newBoxLoop0For
#define hypre_BoxLoop0End        hypre_newBoxLoop0End
#define hypre_BoxLoop1Begin      hypre_newBoxLoop1Begin
#define hypre_BoxLoop1For        hypre_newBoxLoop1For
#define hypre_BoxLoop1End        hypre_newBoxLoop1End
#define hypre_BoxLoop2Begin      hypre_newBoxLoop2Begin
#define hypre_BoxLoop2For        hypre_newBoxLoop2For
#define hypre_BoxLoop2End        hypre_newBoxLoop2End
#define hypre_BoxLoop3Begin      hypre_newBoxLoop3Begin
#define hypre_BoxLoop3For        hypre_newBoxLoop3For
#define hypre_BoxLoop3End        hypre_newBoxLoop3End
#define hypre_BoxLoop4Begin      hypre_newBoxLoop4Begin
#define hypre_BoxLoop4For        hypre_newBoxLoop4For
#define hypre_BoxLoop4End        hypre_newBoxLoop4End

#define hypre_BasicBoxLoop2Begin zypre_newBasicBoxLoop2Begin
#endif
