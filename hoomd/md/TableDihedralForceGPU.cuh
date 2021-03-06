// Copyright (c) 2009-2016 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: phillicl

/*! \file TableDihedralForceGPU.cuh
    \brief Declares GPU kernel code for calculating the table bond forces. Used by TableDihedralForceGPU.
*/

#include "hoomd/ParticleData.cuh"
#include "hoomd/BondedGroupData.cuh"
#include "hoomd/Index1D.h"
#include "hoomd/HOOMDMath.h"

#ifndef __TABLEDIHEDRALFORCECOMPUTEGPU_CUH__
#define __TABLEDIHEDRALFORCECOMPUTEGPU_CUH__

//! Kernel driver that computes table forces on the GPU for TableDihedralForceGPU
cudaError_t gpu_compute_table_dihedral_forces(Scalar4* d_force,
                                     Scalar* d_virial,
                                     const unsigned int virial_pitch,
                                     const unsigned int N,
                                     const Scalar4 *d_pos,
                                     const BoxDim &box,
                                     const group_storage<4> *dlist,
                                     const unsigned int *dihedral_ABCD,
                                     const unsigned int pitch,
                                     const unsigned int *n_dihedrals_list,
                                     const Scalar2 *d_tables,
                                     const unsigned int table_width,
                                     const Index2D &table_value,
                                     const unsigned int block_size,
                                     const unsigned int compute_capability);

#endif
