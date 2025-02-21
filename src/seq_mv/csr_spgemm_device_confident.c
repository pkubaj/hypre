/******************************************************************************
 * Copyright 1998-2019 Lawrence Livermore National Security, LLC and other
 * HYPRE Project Developers. See the top-level COPYRIGHT file for details.
 *
 * SPDX-License-Identifier: (Apache-2.0 OR MIT)
 ******************************************************************************/

/*- - - - - - - - - - - - - - - - - - - - - - - - - - *
           Perform SpMM with Row Nnz Upper Bound
 *- - - - - - - - - - - - - - - - - - - - - - - - - - */
#include "seq_mv.h"
#include "csr_spgemm_device.h"

/*- - - - - - - - - - - - - - - - - - - - - - - - - - *
                Numerical Multiplication
 *- - - - - - - - - - - - - - - - - - - - - - - - - - */
#if defined(HYPRE_USING_CUDA)
#define HYPRE_SPGEMM_NUMER_HASH_SIZE 256
#elif defined(HYPRE_USING_HIP)
#define HYPRE_SPGEMM_NUMER_HASH_SIZE 256
#endif


#if defined(HYPRE_USING_CUDA) || defined(HYPRE_USING_HIP)

template <char HashType, HYPRE_Int FAILED_SYMBL>
static __device__ __forceinline__
HYPRE_Int
hypre_spgemm_hash_insert_numer( HYPRE_Int      HashSize,      /* capacity of the hash table */
                                volatile HYPRE_Int     *HashKeys,      /* assumed to be initialized as all -1's */
                                volatile HYPRE_Complex *HashVals,      /* assumed to be initialized as all 0's */
                                HYPRE_Int      key,           /* assumed to be nonnegative */
                                HYPRE_Complex  val,
                                HYPRE_Int     &count )
{
   HYPRE_Int j = 0;

#pragma unroll
   for (HYPRE_Int i = 0; i < HashSize; i++)
   {
      /* compute the hash value of key */
      if (i == 0)
      {
         j = key & (HashSize - 1);
      }
      else
      {
         j = HashFunc<HashType>(HashSize, key, i, j);
      }

      /* try to insert key+1 into slot j */
      HYPRE_Int old = atomicCAS((HYPRE_Int *)(HashKeys + j), -1, key);

      if (old == -1 || old == key)
      {
         if (FAILED_SYMBL)
         {
            if (old == -1)
            {
               count++;
            }
         }
         /* this slot was open or contained 'key', update value */
         atomicAdd((HYPRE_Complex*)(HashVals + j), val);
         return j;
      }
   }

   return -1;
}

template <HYPRE_Int FAILED_SYMBL, char HashType>
static __device__ __forceinline__
HYPRE_Int
hypre_spgemm_compute_row_numer( HYPRE_Int      rowi,
                                HYPRE_Int      lane_id,
                                HYPRE_Int     *ia,
                                HYPRE_Int     *ja,
                                HYPRE_Complex *aa,
                                HYPRE_Int     *ib,
                                HYPRE_Int     *jb,
                                HYPRE_Complex *ab,
                                HYPRE_Int      s_HashSize,
                                volatile HYPRE_Int     *s_HashKeys,
                                volatile HYPRE_Complex *s_HashVals,
                                HYPRE_Int      g_HashSize,
                                HYPRE_Int     *g_HashKeys,
                                HYPRE_Complex *g_HashVals)
{
   /* load the start and end position of row i of A */
   HYPRE_Int i = 0;

   if (lane_id < 2)
   {
      i = read_only_load(ia + rowi + lane_id);
   }
   const HYPRE_Int istart = __shfl_sync(HYPRE_WARP_FULL_MASK, i, 0);
   const HYPRE_Int iend   = __shfl_sync(HYPRE_WARP_FULL_MASK, i, 1);

   HYPRE_Int num_new_insert = 0;

   /* load column idx and values of row i of A */
   for (i = istart; i < iend; i += blockDim.y)
   {
      HYPRE_Int     colA = -1;
      HYPRE_Complex valA = 0.0;

      if (threadIdx.x == 0 && i + threadIdx.y < iend)
      {
         colA = read_only_load(ja + i + threadIdx.y);
         valA = read_only_load(aa + i + threadIdx.y);
      }

#if 0
      //const HYPRE_Int ymask = get_mask<4>(lane_id);
      // TODO: need to confirm the behavior of __ballot_sync, leave it here for now
      //const HYPRE_Int num_valid_rows = __popc(__ballot_sync(ymask, valid_i));
      //for (HYPRE_Int j = 0; j < num_valid_rows; j++)
#endif

      /* threads in the same ygroup work on one row together */
      const HYPRE_Int     rowB = __shfl_sync(HYPRE_WARP_FULL_MASK, colA, 0, blockDim.x);
      const HYPRE_Complex mult = __shfl_sync(HYPRE_WARP_FULL_MASK, valA, 0, blockDim.x);
      /* open this row of B, collectively */
      HYPRE_Int tmp = 0;
      if (rowB != -1 && threadIdx.x < 2)
      {
         tmp = read_only_load(ib + rowB + threadIdx.x);
      }
      const HYPRE_Int rowB_start = __shfl_sync(HYPRE_WARP_FULL_MASK, tmp, 0, blockDim.x);
      const HYPRE_Int rowB_end   = __shfl_sync(HYPRE_WARP_FULL_MASK, tmp, 1, blockDim.x);

      for (HYPRE_Int k = rowB_start + threadIdx.x; __any_sync(HYPRE_WARP_FULL_MASK, k < rowB_end);
           k += blockDim.x)
      {
         if (k < rowB_end)
         {
            const HYPRE_Int     k_idx = read_only_load(jb + k);
            const HYPRE_Complex k_val = read_only_load(ab + k) * mult;
            /* first try to insert into shared memory hash table */
            HYPRE_Int pos = hypre_spgemm_hash_insert_numer<HashType, FAILED_SYMBL>
                            (s_HashSize, s_HashKeys, s_HashVals, k_idx, k_val, num_new_insert);

            if (-1 == pos)
            {
               pos = hypre_spgemm_hash_insert_numer<HashType, FAILED_SYMBL>
                     (g_HashSize, g_HashKeys, g_HashVals, k_idx, k_val, num_new_insert);
            }

            hypre_device_assert(pos != -1);
         }
      }
   }

   return num_new_insert;
}

template <HYPRE_Int NUM_WARPS_PER_BLOCK, HYPRE_Int SHMEM_HASH_SIZE>
static __device__ __forceinline__
HYPRE_Int
hypre_spgemm_copy_from_hash_into_C_row( HYPRE_Int      lane_id,
                                        volatile HYPRE_Int     *s_HashKeys,
                                        volatile HYPRE_Complex *s_HashVals,
                                        HYPRE_Int      ghash_size,
                                        HYPRE_Int     *jg_start,
                                        HYPRE_Complex *ag_start,
                                        HYPRE_Int     *jc_start,
                                        HYPRE_Complex *ac_start)
{
   HYPRE_Int j = 0;

   /* copy shared memory hash table into C */
#pragma unroll
   for (HYPRE_Int k = lane_id; k < SHMEM_HASH_SIZE; k += HYPRE_WARP_SIZE)
   {
      HYPRE_Int key, sum, pos;
      key = s_HashKeys[k];
      HYPRE_Int in = key != -1;
      pos = warp_prefix_sum(lane_id, in, sum);
      if (key != -1)
      {
         jc_start[j + pos] = key;
         ac_start[j + pos] = s_HashVals[k];
      }
      j += sum;
   }

   /* copy global memory hash table into C */
#pragma unroll
   for (HYPRE_Int k = 0; k < ghash_size; k += HYPRE_WARP_SIZE)
   {
      HYPRE_Int key = -1, sum, pos;
      if (k + lane_id < ghash_size)
      {
         key = jg_start[k + lane_id];
      }
      HYPRE_Int in = key != -1;
      pos = warp_prefix_sum(lane_id, in, sum);
      if (key != -1)
      {
         jc_start[j + pos] = key;
         ac_start[j + pos] = ag_start[k + lane_id];
      }
      j += sum;
   }

   return j;
}

template <HYPRE_Int NUM_WARPS_PER_BLOCK, HYPRE_Int SHMEM_HASH_SIZE, HYPRE_Int FAILED_SYMBL, char HashType>
__global__ void
hypre_spgemm_numeric( HYPRE_Int      M, /* HYPRE_Int K, HYPRE_Int N, */
                      HYPRE_Int     *ia,
                      HYPRE_Int     *ja,
                      HYPRE_Complex *aa,
                      HYPRE_Int     *ib,
                      HYPRE_Int     *jb,
                      HYPRE_Complex *ab,
                      HYPRE_Int     *ic,
                      HYPRE_Int     *jc,
                      HYPRE_Complex *ac,
                      HYPRE_Int     *rc,
                      HYPRE_Int     *ig,
                      HYPRE_Int     *jg,
                      HYPRE_Complex *ag)
{
   volatile const HYPRE_Int num_warps = NUM_WARPS_PER_BLOCK * gridDim.x;
   /* warp id inside the block */
   volatile const HYPRE_Int warp_id = get_warp_id();
   /* warp id in the grid */
   volatile const HYPRE_Int grid_warp_id = blockIdx.x * NUM_WARPS_PER_BLOCK + warp_id;
   /* lane id inside the warp */
   volatile HYPRE_Int lane_id = get_lane_id();
   /* shared memory hash table */
#if 1
   __shared__ volatile HYPRE_Int     s_HashKeys[NUM_WARPS_PER_BLOCK * SHMEM_HASH_SIZE];
   __shared__ volatile HYPRE_Complex s_HashVals[NUM_WARPS_PER_BLOCK * SHMEM_HASH_SIZE];
#else
   extern __shared__ volatile HYPRE_Int shared_mem[];
   volatile HYPRE_Int *s_HashKeys = shared_mem;
   volatile HYPRE_Complex *s_HashVals = (volatile HYPRE_Complex *) &s_HashKeys[NUM_WARPS_PER_BLOCK *
                                                                                                   SHMEM_HASH_SIZE];
#endif
   /* shared memory hash table for this warp */
   volatile HYPRE_Int     *warp_s_HashKeys = s_HashKeys + warp_id * SHMEM_HASH_SIZE;
   volatile HYPRE_Complex *warp_s_HashVals = s_HashVals + warp_id * SHMEM_HASH_SIZE;

   hypre_device_assert(blockDim.z              == NUM_WARPS_PER_BLOCK);
   hypre_device_assert(blockDim.x * blockDim.y == HYPRE_WARP_SIZE);

   /* a warp working on the ith row */
   for (HYPRE_Int i = grid_warp_id; i < M; i += num_warps)
   {
      /* start/end position of global memory hash table */
      HYPRE_Int j = -1, istart_g = 0, iend_g = 0, ghash_size = 0, jsum;

      if (ig)
      {
         if (lane_id < 2)
         {
            j = read_only_load(ig + grid_warp_id + lane_id);
         }
         istart_g = __shfl_sync(HYPRE_WARP_FULL_MASK, j, 0);
         iend_g   = __shfl_sync(HYPRE_WARP_FULL_MASK, j, 1);

         /* size of global hash table allocated for this row
            (must be power of 2 and >= the actual size of the row of C) */
         ghash_size = iend_g - istart_g;

         /* initialize warp's global memory hash table */
#pragma unroll
         for (HYPRE_Int k = lane_id; k < ghash_size; k += HYPRE_WARP_SIZE)
         {
            jg[istart_g + k] = -1;
            ag[istart_g + k] = 0.0;
         }
      }

      /* initialize warp's shared memory hash table */
#pragma unroll
      for (HYPRE_Int k = lane_id; k < SHMEM_HASH_SIZE; k += HYPRE_WARP_SIZE)
      {
         warp_s_HashKeys[k] = -1;
         warp_s_HashVals[k] = 0.0;
      }

      __syncwarp();

      /* work with two hash tables. jsum is the (exact) nnz for row i */
      jsum = hypre_spgemm_compute_row_numer<FAILED_SYMBL, HashType>(i, lane_id, ia, ja, aa, ib, jb, ab,
                                                                    SHMEM_HASH_SIZE, warp_s_HashKeys,
                                                                    warp_s_HashVals,
                                                                    ghash_size, jg + istart_g, ag + istart_g);

      if (FAILED_SYMBL)
      {
         /* in the case when symb mult was failed, save row nnz into rc */
         /* num of nonzeros of this row of C (exact) */
         jsum = warp_reduce_sum(jsum);
         if (lane_id == 0)
         {
            rc[i] = jsum;
         }
      }

      /* copy results into the final C */
      /* start/end position in C */
#ifdef HYPRE_DEBUG
      if (lane_id < 2)
      {
         j = read_only_load(ic + i + lane_id);
      }
      HYPRE_Int istart_c = __shfl_sync(HYPRE_WARP_FULL_MASK, j, 0);
      HYPRE_Int iend_c   = __shfl_sync(HYPRE_WARP_FULL_MASK, j, 1);
#else
      if (lane_id < 1)
      {
         j = read_only_load(ic + i);
      }
      HYPRE_Int istart_c = __shfl_sync(HYPRE_WARP_FULL_MASK, j, 0);
#endif

      j = hypre_spgemm_copy_from_hash_into_C_row<NUM_WARPS_PER_BLOCK, SHMEM_HASH_SIZE>
          (lane_id, warp_s_HashKeys, warp_s_HashVals, ghash_size, jg + istart_g,
           ag + istart_g, jc + istart_c, ac + istart_c);

#if defined(HYPRE_DEBUG)
      if (FAILED_SYMBL)
      {
         hypre_device_assert(istart_c + j <= iend_c);
      }
      else
      {
         hypre_device_assert(istart_c + j == iend_c);
      }
#endif
   }
}

template <HYPRE_Int NUM_WARPS_PER_BLOCK>
__global__ void
hypre_spgemm_copy_from_Cext_into_C( HYPRE_Int      M,
                                    HYPRE_Int     *ix,
                                    HYPRE_Int     *jx,
                                    HYPRE_Complex *ax,
                                    HYPRE_Int     *ic,
                                    HYPRE_Int     *jc,
                                    HYPRE_Complex *ac )
{
   const HYPRE_Int num_warps = NUM_WARPS_PER_BLOCK * gridDim.x;
   /* warp id inside the block */
   const HYPRE_Int warp_id = get_warp_id();
   /* lane id inside the warp */
   volatile const HYPRE_Int lane_id = get_lane_id();

   hypre_device_assert(blockDim.x * blockDim.y == HYPRE_WARP_SIZE);

   for (HYPRE_Int i = blockIdx.x * NUM_WARPS_PER_BLOCK + warp_id;
        i < M;
        i += num_warps)
   {
      HYPRE_Int kc = 0, kx = 0;

      /* start/end position in C and X*/
      if (lane_id < 2)
      {
         kc = read_only_load(ic + i + lane_id);
         kx = read_only_load(ix + i + lane_id);
      }
      HYPRE_Int istart_c = __shfl_sync(HYPRE_WARP_FULL_MASK, kc, 0);
      HYPRE_Int iend_c   = __shfl_sync(HYPRE_WARP_FULL_MASK, kc, 1);
      HYPRE_Int istart_x = __shfl_sync(HYPRE_WARP_FULL_MASK, kx, 0);
#if defined(HYPRE_DEBUG)
      HYPRE_Int iend_x   = __shfl_sync(HYPRE_WARP_FULL_MASK, kx, 1);
      hypre_device_assert(iend_c - istart_c <= iend_x - istart_x);
#endif

      HYPRE_Int p = istart_x - istart_c;
      for (HYPRE_Int k = istart_c + lane_id; k < iend_c; k += HYPRE_WARP_SIZE)
      {
         jc[k] = jx[k + p];
         ac[k] = ax[k + p];
      }
   }
}

/* SpGeMM with Rownnz/Upper bound */
template <HYPRE_Int shmem_hash_size, HYPRE_Int exact_rownnz, char hash_type>
HYPRE_Int
hypre_spgemm_numerical_with_rownnz( HYPRE_Int       m,
                                    HYPRE_Int       k,
                                    HYPRE_Int       n,
                                    HYPRE_Int      *d_ia,
                                    HYPRE_Int      *d_ja,
                                    HYPRE_Complex  *d_a,
                                    HYPRE_Int      *d_ib,
                                    HYPRE_Int      *d_jb,
                                    HYPRE_Complex  *d_b,
                                    HYPRE_Int      *d_rc,
                                    HYPRE_Int     **d_ic_out,
                                    HYPRE_Int     **d_jc_out,
                                    HYPRE_Complex **d_c_out,
                                    HYPRE_Int      *nnzC)
{
#ifdef HYPRE_PROFILE
   hypre_profile_times[HYPRE_TIMER_ID_SPMM_NUMERIC] -= hypre_MPI_Wtime();
#endif

#if defined(HYPRE_USING_CUDA)
   const HYPRE_Int num_warps_per_block = 16;
   const HYPRE_Int BDIMX               =  2;
#elif defined(HYPRE_USING_HIP)
   const HYPRE_Int num_warps_per_block = 16;
   const HYPRE_Int BDIMX               =  4;
#endif
   const HYPRE_Int BDIMY               = HYPRE_WARP_SIZE / BDIMX;

#if 0
   const size_t    shmem_size          = num_warps_per_block * shmem_hash_size * (sizeof(
                                                                                     HYPRE_Complex) + sizeof(HYPRE_Int));
   const HYPRE_Int shmem_maxbytes      = 65536;
   hypre_assert(shmem_size <= shmem_maxbytes);
   /* CUDA V100 */
   hypre_int v1, v2;
   cudaDeviceGetAttribute(&v1, cudaDevAttrMaxSharedMemoryPerBlock,
                          hypre_HandleDevice(hypre_handle()));
   cudaDeviceGetAttribute(&v2, cudaDevAttrMaxSharedMemoryPerBlockOptin,
                          hypre_HandleDevice(hypre_handle()));

   if (shmem_maxbytes > 49152)
   {
      HYPRE_CUDA_CALL( cudaFuncSetAttribute(hypre_spgemm_numeric < num_warps_per_block, shmem_hash_size,
                                            !exact_rownnz, hash_type >,
                                            cudaFuncAttributeMaxDynamicSharedMemorySize, shmem_maxbytes) );
   }
#endif

   /* CUDA kernel configurations */
   dim3 bDim(BDIMX, BDIMY, num_warps_per_block);
   hypre_assert(bDim.x * bDim.y == HYPRE_WARP_SIZE);
   // for cases where one WARP works on a row
   HYPRE_Int num_warps = hypre_min(m, HYPRE_MAX_NUM_WARPS);
   dim3 gDim( (num_warps + bDim.z - 1) / bDim.z );
   // number of active warps
   HYPRE_Int num_act_warps = hypre_min(bDim.z * gDim.x, m);

   /* ---------------------------------------------------------------------------
    * build hash table
    * ---------------------------------------------------------------------------*/
   HYPRE_Int     *d_ghash_i = NULL;
   HYPRE_Int     *d_ghash_j = NULL;
   HYPRE_Complex *d_ghash_a = NULL;

   /* RL Note: even with exact_rownnz, still may need global hash, since shared hash has different size from symbol. */
   hypre_SpGemmCreateGlobalHashTable(m, NULL, num_act_warps, d_rc, shmem_hash_size,
                                     &d_ghash_i, &d_ghash_j, &d_ghash_a, NULL, 1);

   /* ---------------------------------------------------------------------------
    * numerical multiplication:
    * ---------------------------------------------------------------------------*/
   /* if rc contains exact_rownnz: can allocate the final C=(ic,jc,c) directly;
      if rc contains upper bound : it is a temporary space that is more than enough to store C */
   HYPRE_Int     *d_ic = hypre_TAlloc(HYPRE_Int, m + 1, HYPRE_MEMORY_DEVICE);
   HYPRE_Int     *d_jc;
   HYPRE_Complex *d_c;
   HYPRE_Int      nnzC_nume;

   hypre_create_ija(m, d_rc, d_ic, &d_jc, &d_c, &nnzC_nume);

   HYPRE_CUDA_LAUNCH ( (hypre_spgemm_numeric < num_warps_per_block, shmem_hash_size, !exact_rownnz,
                        hash_type > ),
                       gDim, bDim, /* shmem_size, */
                       m, /* k, n, */ d_ia, d_ja, d_a, d_ib, d_jb, d_b, d_ic, d_jc, d_c, d_rc,
                       d_ghash_i, d_ghash_j, d_ghash_a );

   /* post-processing */
   if (!exact_rownnz)
   {
      HYPRE_Int nnzC_nume_new = hypreDevice_IntegerReduceSum(m, d_rc);

      hypre_assert(nnzC_nume_new <= nnzC_nume);

      if (nnzC_nume_new < nnzC_nume)
      {
         HYPRE_Int     *d_ic_new = hypre_TAlloc(HYPRE_Int, m + 1, HYPRE_MEMORY_DEVICE);
         HYPRE_Int     *d_jc_new;
         HYPRE_Complex *d_c_new;
         HYPRE_Int      tmp;

         /* alloc final C */
         hypre_create_ija(m, d_rc, d_ic_new, &d_jc_new, &d_c_new, &tmp);
         hypre_assert(tmp == nnzC_nume_new);

         /* copy to the final C */
         dim3 gDim( (m + bDim.z - 1) / bDim.z );
         HYPRE_CUDA_LAUNCH( (hypre_spgemm_copy_from_Cext_into_C<num_warps_per_block>), gDim, bDim,
                            m, d_ic, d_jc, d_c, d_ic_new, d_jc_new, d_c_new );

         hypre_TFree(d_ic, HYPRE_MEMORY_DEVICE);
         hypre_TFree(d_jc, HYPRE_MEMORY_DEVICE);
         hypre_TFree(d_c,  HYPRE_MEMORY_DEVICE);

         d_ic = d_ic_new;
         d_jc = d_jc_new;
         d_c  = d_c_new;
         nnzC_nume = nnzC_nume_new;
      }
   }

   hypre_TFree(d_ghash_i, HYPRE_MEMORY_DEVICE);
   hypre_TFree(d_ghash_j, HYPRE_MEMORY_DEVICE);
   hypre_TFree(d_ghash_a, HYPRE_MEMORY_DEVICE);

   *d_ic_out = d_ic;
   *d_jc_out = d_jc;
   *d_c_out  = d_c;
   *nnzC     = nnzC_nume;

#ifdef HYPRE_PROFILE
   cudaThreadSynchronize();
   hypre_profile_times[HYPRE_TIMER_ID_SPMM_NUMERIC] += hypre_MPI_Wtime();
#endif

   return hypre_error_flag;
}

HYPRE_Int
hypreDevice_CSRSpGemmNumerWithRownnzUpperbound( HYPRE_Int       m,
                                                HYPRE_Int       k,
                                                HYPRE_Int       n,
                                                HYPRE_Int      *d_ia,
                                                HYPRE_Int      *d_ja,
                                                HYPRE_Complex  *d_a,
                                                HYPRE_Int      *d_ib,
                                                HYPRE_Int      *d_jb,
                                                HYPRE_Complex  *d_b,
                                                HYPRE_Int      *d_rc,         /* input: nnz (upper bound) of each row */
                                                HYPRE_Int       exact_rownnz, /* if d_rc is exact       */
                                                HYPRE_Int     **d_ic_out,
                                                HYPRE_Int     **d_jc_out,
                                                HYPRE_Complex **d_c_out,
                                                HYPRE_Int      *nnzC )
{
   const HYPRE_Int shmem_hash_size = HYPRE_SPGEMM_NUMER_HASH_SIZE;
   const char      hash_type       = hypre_HandleSpgemmHashType(hypre_handle());

   if (hash_type != 'L' && hash_type != 'Q' && hash_type != 'D')
   {
      hypre_printf("Unrecognized hash type ... [L(inear), Q(uadratic), D(ouble)]\n");
      exit(0);
   }

   //HYPRE_Int max_rc = HYPRE_THRUST_CALL(reduce, d_rc, d_rc + m, 0, thrust::maximum<HYPRE_Int>());
   //hypre_printf("max_rc numerical %d\n", max_rc);

   if (exact_rownnz)
   {
      if (hash_type == 'L')
      {
         hypre_spgemm_numerical_with_rownnz<shmem_hash_size, 1, 'L'>
         (m, k, n, d_ia, d_ja, d_a, d_ib, d_jb, d_b, d_rc, d_ic_out, d_jc_out, d_c_out, nnzC);
      }
      else if (hash_type == 'Q')
      {
         hypre_spgemm_numerical_with_rownnz<shmem_hash_size, 1, 'Q'>
         (m, k, n, d_ia, d_ja, d_a, d_ib, d_jb, d_b, d_rc, d_ic_out, d_jc_out, d_c_out, nnzC);
      }
      else if (hash_type == 'D')
      {
         hypre_spgemm_numerical_with_rownnz<shmem_hash_size, 1, 'D'>
         (m, k, n, d_ia, d_ja, d_a, d_ib, d_jb, d_b, d_rc, d_ic_out, d_jc_out, d_c_out, nnzC);
      }
   }
   else
   {
      if (hash_type == 'L')
      {
         hypre_spgemm_numerical_with_rownnz<shmem_hash_size, 0, 'L'>
         (m, k, n, d_ia, d_ja, d_a, d_ib, d_jb, d_b, d_rc, d_ic_out, d_jc_out, d_c_out, nnzC);
      }
      else if (hash_type == 'Q')
      {
         hypre_spgemm_numerical_with_rownnz<shmem_hash_size, 0, 'Q'>
         (m, k, n, d_ia, d_ja, d_a, d_ib, d_jb, d_b, d_rc, d_ic_out, d_jc_out, d_c_out, nnzC);
      }
      else if (hash_type == 'D')
      {
         hypre_spgemm_numerical_with_rownnz<shmem_hash_size, 0, 'D'>
         (m, k, n, d_ia, d_ja, d_a, d_ib, d_jb, d_b, d_rc, d_ic_out, d_jc_out, d_c_out, nnzC);
      }
   }

   return hypre_error_flag;
}
#endif /* HYPRE_USING_CUDA  || defined(HYPRE_USING_HIP) */

