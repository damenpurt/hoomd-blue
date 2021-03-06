// Copyright (c) 2009-2016 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: phillicl

/*! \file PotentialPairDPDThermoGPU.cuh
    \brief Declares driver functions for computing all types of pair forces on the GPU
*/

#ifndef __POTENTIAL_PAIR_DPDTHERMO_CUH__
#define __POTENTIAL_PAIR_DPDTHERMO_CUH__

#include "hoomd/TextureTools.h"
#include "hoomd/ParticleData.cuh"
#include "EvaluatorPairDPDThermo.h"
#include "hoomd/Index1D.h"
#include <cassert>

#include "PotentialPairGPU.cuh"

//! Maximum number of threads (width of a dpd_warp)
const unsigned int gpu_dpd_pair_force_max_tpp = 32;

//! args struct for passing additional options to gpu_compute_dpd_forces
struct dpd_pair_args_t
    {
    //! Construct a dpd_pair_args_t
    dpd_pair_args_t(Scalar4 *_d_force,
                    Scalar *_d_virial,
                    const unsigned int _virial_pitch,
                    const unsigned int _N,
                    const unsigned int _n_max,
                    const Scalar4 *_d_pos,
                    const Scalar4 *_d_vel,
                    const unsigned int *_d_tag,
                    const BoxDim& _box,
                    const unsigned int *_d_n_neigh,
                    const unsigned int *_d_nlist,
                    const unsigned int *_d_head_list,
                    const Scalar *_d_rcutsq,
                    const unsigned int _size_nlist,
                    const unsigned int _ntypes,
                    const unsigned int _block_size,
                    const unsigned int _seed,
                    const unsigned int _timestep,
                    const Scalar _deltaT,
                    const Scalar _T,
                    const unsigned int _shift_mode,
                    const unsigned int _compute_virial,
                    const unsigned int _threads_per_particle,
                    const unsigned int _compute_capability,
                    const unsigned int _max_tex1d_width)
                        : d_force(_d_force),
                        d_virial(_d_virial),
                        virial_pitch(_virial_pitch),
                        N(_N),
                        n_max(_n_max),
                        d_pos(_d_pos),
                        d_vel(_d_vel),
                        d_tag(_d_tag),
                        box(_box),
                        d_n_neigh(_d_n_neigh),
                        d_nlist(_d_nlist),
                        d_head_list(_d_head_list),
                        d_rcutsq(_d_rcutsq),
                        size_nlist(_size_nlist),
                        ntypes(_ntypes),
                        block_size(_block_size),
                        seed(_seed),
                        timestep(_timestep),
                        deltaT(_deltaT),
                        T(_T),
                        shift_mode(_shift_mode),
                        compute_virial(_compute_virial),
                        threads_per_particle(_threads_per_particle),
                        compute_capability(_compute_capability),
                        max_tex1d_width(_max_tex1d_width)
        {
        };

    Scalar4 *d_force;                //!< Force to write out
    Scalar *d_virial;                //!< Virial to write out
    const unsigned int virial_pitch; //!< Pitch of 2D virial array
    const unsigned int N;           //!< number of particles
    const unsigned int n_max;       //!< Maximum size of particle data arrays
    const Scalar4 *d_pos;           //!< particle positions
    const Scalar4 *d_vel;           //!< particle velocities
    const unsigned int *d_tag;      //!< particle tags
    const BoxDim& box;         //!< Simulation box in GPU format
    const unsigned int *d_n_neigh;  //!< Device array listing the number of neighbors on each particle
    const unsigned int *d_nlist;    //!< Device array listing the neighbors of each particle
    const unsigned int *d_head_list;//!< Indexes for accessing d_nlist
    const Scalar *d_rcutsq;          //!< Device array listing r_cut squared per particle type pair
    const unsigned int size_nlist;  //!< Total length of the neighbor list
    const unsigned int ntypes;      //!< Number of particle types in the simulation
    const unsigned int block_size;  //!< Block size to execute
    const unsigned int seed;        //!< user provided seed for PRNG
    const unsigned int timestep;    //!< timestep of simulation
    const Scalar deltaT;             //!< timestep size
    const Scalar T;                  //!< temperature
    const unsigned int shift_mode;  //!< The potential energy shift mode
    const unsigned int compute_virial;  //!< Flag to indicate if virials should be computed
    const unsigned int threads_per_particle; //!< Number of threads per particle (maximum: 32==1 warp)
    const unsigned int compute_capability;  //!< Compute capability of the device (20, 30, 35, ...)
    const unsigned int max_tex1d_width;     //!< Maximum width of a 1d linear texture
    };

#ifdef NVCC

//! Texture for reading particle positions
scalar4_tex_t pdata_dpd_pos_tex;

//! Texture for reading particle velocities
scalar4_tex_t pdata_dpd_vel_tex;

//! Texture for reading particle tags
texture<unsigned int, 1, cudaReadModeElementType> pdata_dpd_tag_tex;

//! Texture for reading neighbor list
texture<unsigned int, 1, cudaReadModeElementType> nlist_tex;

//! Kernel for calculating pair forces
/*! This kernel is called to calculate the pair forces on all N particles. Actual evaluation of the potentials and
    forces for each pair is handled via the template class \a evaluator.

    \param d_force Device memory to write computed forces
    \param d_virial Device memory to write computed virials
    \param virial_pitch Pitch of 2D virial array
    \param N number of particles
    \param d_pos particle positions on the GPU
    \param d_vel particle velocities on the GPU
    \param d_tag particle tags on the GPU
    \param box Box dimensions used to implement periodic boundary conditions
    \param d_n_neigh Device memory array listing the number of neighbors for each particle
    \param d_nlist Device memory array containing the neighbor list contents
    \param d_head_list Indexes for indexing \a d_nlist
    \param d_params Parameters for the potential, stored per type pair
    \param d_rcutsq rcut squared, stored per type pair
    \param d_seed user defined seed for PRNG
    \param d_timestep timestep of simulation
    \param d_deltaT timestep size
    \param d_T temperature
    \param ntypes Number of types in the simulation
    \param tpp Number of threads per particle

    \a d_params, and \a d_rcutsq must be indexed with an Index2DUpperTriangler(typei, typej) to access the
    unique value for that type pair. These values are all cached into shared memory for quick access, so a dynamic
    amount of shared memory must be allocatd for this kernel launch. The amount is
    (2*sizeof(Scalar) + sizeof(typename evaluator::param_type)) * typpair_idx.getNumElements()

    Certain options are controlled via template parameters to avoid the performance hit when they are not enabled.
    \tparam evaluator EvaluatorPair class to evualuate V(r) and -delta V(r)/r
    \tparam shift_mode 0: No energy shifting is done. 1: V(r) is shifted to be 0 at rcut.
    \tparam compute_virial When non-zero, the virial tensor is computed. When zero, the virial tensor is not computed.
    \tparam use_gmem_nlist When non-zero, the neighbor list is read out of global memory. When zero, textures or __ldg
                           is used depending on architecture.

    <b>Implementation details</b>
    Each block will calculate the forces on a block of particles.
    Each thread will calculate the total force on one particle.
*/
template< class evaluator, unsigned int shift_mode, unsigned int compute_virial, unsigned char use_gmem_nlist>
__global__ void gpu_compute_dpd_forces_kernel(Scalar4 *d_force,
                                              Scalar *d_virial,
                                              const unsigned int virial_pitch,
                                              const unsigned int N,
                                              const Scalar4 *d_pos,
                                              const Scalar4 *d_vel,
                                              const unsigned int *d_tag,
                                              BoxDim box,
                                              const unsigned int *d_n_neigh,
                                              const unsigned int *d_nlist,
                                              const unsigned int *d_head_list,
                                              const typename evaluator::param_type *d_params,
                                              const Scalar *d_rcutsq,
                                              const unsigned int d_seed,
                                              const unsigned int d_timestep,
                                              const Scalar d_deltaT,
                                              const Scalar d_T,
                                              const int ntypes,
                                              const unsigned int tpp)
    {
    Index2D typpair_idx(ntypes);
    const unsigned int num_typ_parameters = typpair_idx.getNumElements();

    // shared arrays for per type pair parameters
    extern __shared__ char s_data[];
    typename evaluator::param_type *s_params =
        (typename evaluator::param_type *)(&s_data[0]);
    Scalar *s_rcutsq = (Scalar *)(&s_data[num_typ_parameters*sizeof(evaluator::param_type)]);

    // load in the per type pair parameters
    for (unsigned int cur_offset = 0; cur_offset < num_typ_parameters; cur_offset += blockDim.x)
        {
        if (cur_offset + threadIdx.x < num_typ_parameters)
            {
            s_rcutsq[cur_offset + threadIdx.x] = d_rcutsq[cur_offset + threadIdx.x];
            s_params[cur_offset + threadIdx.x] = d_params[cur_offset + threadIdx.x];
            }
        }
    __syncthreads();

    // start by identifying which particle we are to handle
    unsigned int idx;
    if (gridDim.y > 1)
        {
        // if we have blocks in the y-direction, the fermi-workaround is in place
        idx = (blockIdx.x + blockIdx.y * 65535) * (blockDim.x/tpp) + threadIdx.x/tpp;
        }
    else
        {
        idx = blockIdx.x * (blockDim.x/tpp) + threadIdx.x/tpp;
        }

    bool active = true;

    if (idx >= N)
        {
        // need to mask this thread, but still participate in warp-level reduction (because of __syncthreads())
        active = false;
        }

    // initialize the force to 0
    Scalar4 force = make_scalar4(Scalar(0.0), Scalar(0.0), Scalar(0.0), Scalar(0.0));
    Scalar virial[6];
    for (unsigned int i = 0; i < 6; i++)
        virial[i] = Scalar(0.0);

    if (active)
        {
        // load in the length of the neighbor list (MEM_TRANSFER: 4 bytes)
        unsigned int n_neigh = d_n_neigh[idx];

        // read in the position of our particle.
        // (MEM TRANSFER: 16 bytes)
        Scalar4 postypei = texFetchScalar4(d_pos, pdata_dpd_pos_tex, idx);
        Scalar3 posi = make_scalar3(postypei.x, postypei.y, postypei.z);

        // read in the velocity of our particle.
        // (MEM TRANSFER: 16 bytes)
        Scalar4 velmassi = texFetchScalar4(d_vel, pdata_dpd_vel_tex, idx);
        Scalar3 veli = make_scalar3(velmassi.x, velmassi.y, velmassi.z);

        // prefetch neighbor index
        const unsigned int head_idx = d_head_list[idx];
        unsigned int cur_j = 0;
        unsigned int next_j(0);
        if (use_gmem_nlist)
            {
            next_j = (threadIdx.x%tpp < n_neigh) ? d_nlist[head_idx + threadIdx.x%tpp] : 0;
            }
        else
            {
            next_j = (threadIdx.x%tpp < n_neigh) ? texFetchUint(d_nlist, nlist_tex, head_idx + threadIdx.x%tpp) : 0;
            }

        // this particle's tag
        unsigned int tagi = d_tag[idx];

        // loop over neighbors
        for (int neigh_idx = threadIdx.x%tpp; neigh_idx < n_neigh; neigh_idx+=tpp)
            {
                {
                // read the current neighbor index (MEM TRANSFER: 4 bytes)
                // prefetch the next value and set the current one
                cur_j = next_j;
                if (neigh_idx+tpp < n_neigh)
                    {
                    if (use_gmem_nlist)
                        {
                        next_j = d_nlist[head_idx + neigh_idx + tpp];
                        }
                    else
                        {
                        next_j = texFetchUint(d_nlist, nlist_tex, head_idx + neigh_idx + tpp);
                        }
                    }

                // get the neighbor's position (MEM TRANSFER: 16 bytes)
                Scalar4 postypej = texFetchScalar4(d_pos, pdata_dpd_pos_tex, cur_j);
                Scalar3 posj = make_scalar3(postypej.x, postypej.y, postypej.z);

                // get the neighbor's position (MEM TRANSFER: 16 bytes)
                Scalar4 velmassj = texFetchScalar4(d_vel, pdata_dpd_vel_tex, cur_j);
                Scalar3 velj = make_scalar3(velmassj.x, velmassj.y, velmassj.z);

                // calculate dr (with periodic boundary conditions) (FLOPS: 3)
                Scalar3 dx = posi - posj;

                // apply periodic boundary conditions: (FLOPS 12)
                dx = box.minImage(dx);

                // calculate r squard (FLOPS: 5)
                Scalar rsq = dot(dx,dx);

                // calculate dv (FLOPS: 3)
                Scalar3 dv = veli - velj;

                Scalar rdotv = dot(dx, dv);

                // access the per type pair parameters
                unsigned int typpair = typpair_idx(__scalar_as_int(postypei.w), __scalar_as_int(postypej.w));
                Scalar rcutsq = s_rcutsq[typpair];
                typename evaluator::param_type param = s_params[typpair];

                // design specifies that energies are shifted if
                // 1) shift mode is set to shift
                // or 2) shift mode is explor and ron > rcut
                bool energy_shift = false;
                if (shift_mode == 1)
                    energy_shift = true;

                evaluator eval(rsq, rcutsq, param);

                // evaluate the potential
                Scalar force_divr = Scalar(0.0);
                Scalar force_divr_cons = Scalar(0.0);
                Scalar pair_eng = Scalar(0.0);

                // Special Potential Pair DPD Requirements
                // use particle i's and j's tags
                unsigned int tagj = texFetchUint(d_tag, pdata_dpd_tag_tex, cur_j);
                eval.set_seed_ij_timestep(d_seed,tagi,tagj,d_timestep);
                eval.setDeltaT(d_deltaT);
                eval.setRDotV(rdotv);
                eval.setT(d_T);

                eval.evalForceEnergyThermo(force_divr, force_divr_cons, pair_eng, energy_shift);

                // calculate the virial (FLOPS: 3)
                if (compute_virial)
                    {
                    Scalar force_div2r_cons = Scalar(0.5) * force_divr_cons;
                    virial[0] = dx.x * dx.x * force_div2r_cons;
                    virial[1] = dx.x * dx.y * force_div2r_cons;
                    virial[2] = dx.x * dx.z * force_div2r_cons;
                    virial[3] = dx.y * dx.y * force_div2r_cons;
                    virial[4] = dx.y * dx.z * force_div2r_cons;
                    virial[5] = dx.z * dx.z * force_div2r_cons;
                    }

                // add up the force vector components (FLOPS: 7)
                force.x += dx.x * force_divr;
                force.y += dx.y * force_divr;
                force.z += dx.z * force_divr;

                force.w += pair_eng;
                }
            }

        // potential energy per particle must be halved
        force.w *= Scalar(0.5);
        }

    // we need to access a separate portion of shared memory to avoid race conditions
    const unsigned int shared_bytes = (sizeof(Scalar) + sizeof(typename evaluator::param_type))
        * num_typ_parameters;

    // need to declare as volatile, because we are using warp-synchronous programming
    volatile Scalar *sh = (Scalar *) &s_data[shared_bytes];

    unsigned int cta_offs = (threadIdx.x/tpp)*tpp;

    // reduce force over threads in cta
    force.x = warp_reduce(tpp, threadIdx.x % tpp, force.x, &sh[cta_offs]);
    force.y = warp_reduce(tpp, threadIdx.x % tpp, force.y, &sh[cta_offs]);
    force.z = warp_reduce(tpp, threadIdx.x % tpp, force.z, &sh[cta_offs]);
    force.w = warp_reduce(tpp, threadIdx.x % tpp, force.w, &sh[cta_offs]);

    // now that the force calculation is complete, write out the result (MEM TRANSFER: 20 bytes)
    if (active && threadIdx.x % tpp == 0)
        d_force[idx] = force;

    if (compute_virial)
        {
        for (unsigned int i = 0; i < 6; ++i)
            virial[i] = warp_reduce(tpp, threadIdx.x % tpp, virial[0], &sh[cta_offs]);

        // if we are the first thread in the cta, write out virial to global mem
        if (active && threadIdx.x %tpp == 0)
            for (unsigned int i = 0; i < 6; i++) d_virial[i*virial_pitch+idx] = virial[i];
        }
    }

template<typename T>
int dpd_get_max_block_size(T func)
    {
    cudaFuncAttributes attr;
    cudaFuncGetAttributes(&attr, (const void *)func);
    int max_threads = attr.maxThreadsPerBlock;
    // number of threads has to be multiple of warp size
    max_threads -= max_threads % gpu_dpd_pair_force_max_tpp;
    return max_threads;
    }

inline void gpu_dpd_pair_force_bind_textures(const dpd_pair_args_t pair_args)
    {
    // bind the position texture
    pdata_dpd_pos_tex.normalized = false;
    pdata_dpd_pos_tex.filterMode = cudaFilterModePoint;
    cudaBindTexture(0, pdata_dpd_pos_tex, pair_args.d_pos, sizeof(Scalar4)*pair_args.n_max);

    // bind the diamter texture
    pdata_dpd_vel_tex.normalized = false;
    pdata_dpd_vel_tex.filterMode = cudaFilterModePoint;
    cudaBindTexture(0, pdata_dpd_vel_tex, pair_args.d_vel, sizeof(Scalar4) * pair_args.n_max);

    pdata_dpd_tag_tex.normalized = false;
    pdata_dpd_tag_tex.filterMode = cudaFilterModePoint;
    cudaBindTexture(0, pdata_dpd_tag_tex, pair_args.d_tag, sizeof(unsigned int) * pair_args.n_max);

    if (pair_args.size_nlist <= pair_args.max_tex1d_width)
        {
        nlist_tex.normalized = false;
        nlist_tex.filterMode = cudaFilterModePoint;
        cudaBindTexture(0, nlist_tex, pair_args.d_nlist, sizeof(unsigned int) * pair_args.size_nlist);
        }
    }

//! Templated launcher for the dpd force kernel
template< class evaluator, unsigned int shift_mode, unsigned int compute_virial, unsigned char use_gmem_nlist>
inline void launch_gpu_compute_dpd_forces_kernel(const dpd_pair_args_t& args,
                                                 const typename evaluator::param_type *d_params)
    {
    // setup the grid to run the kernel
    unsigned int block_size = args.block_size;
    unsigned int tpp = args.threads_per_particle;

    Index2D typpair_idx(args.ntypes);
    unsigned int shared_bytes = (sizeof(Scalar) + sizeof(typename evaluator::param_type))
                                * typpair_idx.getNumElements();

    static unsigned int max_block_size = UINT_MAX;
    if (max_block_size == UINT_MAX)
        max_block_size = dpd_get_max_block_size(gpu_compute_dpd_forces_kernel<evaluator, shift_mode, compute_virial, use_gmem_nlist>);

    if (args.compute_capability < 35) gpu_dpd_pair_force_bind_textures(args);

    block_size = block_size < max_block_size ? block_size : max_block_size;
    dim3 grid(args.N / (block_size/tpp) + 1, 1, 1);
    if (args.compute_capability < 30 && grid.x > 65535)
        {
        grid.y = grid.x/65535 + 1;
        grid.x = 65535;
        }

    if (args.compute_capability < 30)
        {
        shared_bytes += sizeof(Scalar)*block_size;
        }

    gpu_compute_dpd_forces_kernel<evaluator, shift_mode, compute_virial, use_gmem_nlist>
                        <<<grid, block_size, shared_bytes>>>
                        (args.d_force,
                        args.d_virial,
                        args.virial_pitch,
                        args.N,
                        args.d_pos,
                        args.d_vel,
                        args.d_tag,
                        args.box,
                        args.d_n_neigh,
                        args.d_nlist,
                        args.d_head_list,
                        d_params,
                        args.d_rcutsq,
                        args.seed,
                        args.timestep,
                        args.deltaT,
                        args.T,
                        args.ntypes,
                        tpp);
    }


//! Kernel driver that computes pair DPD thermo forces on the GPU
/*! \param args Additional options
    \param d_params Per type-pair parameters for the evaluator

    This is just a driver function for gpu_compute_dpd_forces_kernel(), see it for details.
*/
template< class evaluator >
cudaError_t gpu_compute_dpd_forces(const dpd_pair_args_t& args,
                                   const typename evaluator::param_type *d_params)
    {
    assert(d_params);
    assert(args.d_rcutsq);
    assert(args.ntypes > 0);

    // run the kernel
    if (args.compute_capability < 35 && args.size_nlist > args.max_tex1d_width)
        {
        if (args.compute_virial)
            {
            switch (args.shift_mode)
                {
                case 0:
                    {
                    launch_gpu_compute_dpd_forces_kernel<evaluator, 0, 1, 1>(args, d_params);
                    break;
                    }
                case 1:
                    {
                    launch_gpu_compute_dpd_forces_kernel<evaluator, 1, 1, 1>(args, d_params);
                    break;
                    }
                default:
                    return cudaErrorUnknown;
                }
            }
        else
            {
            switch (args.shift_mode)
                {
                case 0:
                    {
                    launch_gpu_compute_dpd_forces_kernel<evaluator, 0, 0, 1>(args, d_params);
                    break;
                    }
                case 1:
                    {
                    launch_gpu_compute_dpd_forces_kernel<evaluator, 1, 0, 1>(args, d_params);
                    break;
                    }
                default:
                    return cudaErrorUnknown;
                }
            }
        }
    else
        {
        if (args.compute_virial)
            {
            switch (args.shift_mode)
                {
                case 0:
                    {
                    launch_gpu_compute_dpd_forces_kernel<evaluator, 0, 1, 0>(args, d_params);
                    break;
                    }
                case 1:
                    {
                    launch_gpu_compute_dpd_forces_kernel<evaluator, 1, 1, 0>(args, d_params);
                    break;
                    }
                default:
                    return cudaErrorUnknown;
                }
            }
        else
            {
            switch (args.shift_mode)
                {
                case 0:
                    {
                    launch_gpu_compute_dpd_forces_kernel<evaluator, 0, 0, 0>(args, d_params);
                    break;
                    }
                case 1:
                    {
                    launch_gpu_compute_dpd_forces_kernel<evaluator, 1, 0, 0>(args, d_params);
                    break;
                    }
                default:
                    return cudaErrorUnknown;
                }
            }
        }

    return cudaSuccess;
    }
#endif

#endif // __POTENTIAL_PAIR_DPDTHERMO_CUH__
