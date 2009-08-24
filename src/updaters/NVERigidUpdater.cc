/*
Highly Optimized Object-Oriented Molecular Dynamics (HOOMD) Open
Source Software License
Copyright (c) 2008 Ames Laboratory Iowa State University
All rights reserved.

Redistribution and use of HOOMD, in source and binary forms, with or
without modification, are permitted, provided that the following
conditions are met:

* Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names HOOMD's
contributors may be used to endorse or promote products derived from this
software without specific prior written permission.

Disclaimer

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND
CONTRIBUTORS ``AS IS''  AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS  BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/

// $Id: NVERigidUpdater.cc 1478 2008-11-14 19:25:38Z joaander $
// $URL: https://svn2.assembla.com/svn/hoomd/trunk/src/updaters/NVERigidUpdater.cc $

/*! \file NVERigidUpdater.cc
	\brief Defines the NVERigidUpdater class
*/

#ifdef WIN32
#pragma warning( push )
#pragma warning( disable : 4103 4244 )
#endif


#include <boost/python.hpp>

using namespace boost::python;

#include "NVERigidUpdater.h"
#include "SystemDefinition.h"
#include <math.h>

#include <boost/bind.hpp>

using namespace boost::signals;
using namespace boost;

using namespace std;

/*! \param pdata Particle data to update
	\param deltaT Time step to use
*/
NVERigidUpdater::NVERigidUpdater(boost::shared_ptr<SystemDefinition> sysdef, Scalar deltaT) : m_deltaT(deltaT)
	{
	//! Get the system rigid data
	m_rigid_data = sysdef->getRigidData();
		
	//! Get the particle data associated with the rigid data (i.e. the system particle data?)
	m_pdata = sysdef->getParticleData();
	
	//! Get the number of rigid bodies for frequent use
	m_n_bodies = m_rigid_data->getNumBodies();
	
	//! Allocate memory for forces and torques	
	GPUArray<Scalar4> force(m_n_bodies, m_pdata->getExecConf());
	GPUArray<Scalar4> torque(m_n_bodies, m_pdata->getExecConf());	

	// swap the allocated GPUArray with the member variables
	m_force.swap(force);
	m_torque.swap(torque);
		
	}

void NVERigidUpdater::setup()
	{
	// get box
	const BoxDim& box = m_pdata->getBox();
	// sanity check
	assert(box.xhi > box.xlo && box.yhi > box.ylo && box.zhi > box.zlo);	
	
	// precalculate box lenghts
	Scalar Lx = box.xhi - box.xlo;
	Scalar Ly = box.yhi - box.ylo;
	Scalar Lz = box.zhi - box.zlo;
	
	{
	// rigid data handles
	ArrayHandle<unsigned int> body_size_handle(m_rigid_data->getBodySize(), access_location::host, access_mode::read);
	ArrayHandle<unsigned int> particle_indices_handle(m_rigid_data->getParticleIndices(), access_location::host, access_mode::read);
	unsigned int indices_pitch = m_rigid_data->getParticleIndices().getPitch();	
	
	ArrayHandle<Scalar4> com_handle(m_rigid_data->getCOM(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> angmom_handle(m_rigid_data->getAngMom(), access_location::host, access_mode::readwrite);
	ArrayHandle<Scalar4> force_handle(m_force, access_location::host, access_mode::readwrite);
	ArrayHandle<Scalar4> torque_handle(m_torque, access_location::host, access_mode::readwrite);
		
	// Reset all forces and torques
	for (unsigned int body = 0; body < m_n_bodies; body++)
		{
		force_handle.data[body].x = 0.0;
		force_handle.data[body].y = 0.0;
		force_handle.data[body].z = 0.0;
		
		torque_handle.data[body].x = 0.0;
		torque_handle.data[body].y = 0.0;
		torque_handle.data[body].z = 0.0;
		
		angmom_handle.data[body].x = 0.0;
		angmom_handle.data[body].y = 0.0;
		angmom_handle.data[body].z = 0.0;
		}
	
	// Access the particle data arrays
	ParticleDataArrays arrays = m_pdata->acquireReadWrite();
	
	// for each body
	for (unsigned int body = 0; body < m_n_bodies; body++) 
		{
		// for each particle
		unsigned int len = body_size_handle.data[body];
		for (unsigned int j = 0; j < len; j++)
			{
			// get the index of particle in the particle arrays
			unsigned int pidx = particle_indices_handle.data[body * indices_pitch + j];
			// get the particle mass 
			Scalar mass_one = arrays.mass[pidx];
			
			Scalar fx = mass_one * arrays.ax[pidx];
			Scalar fy = mass_one * arrays.ay[pidx];
			Scalar fz = mass_one * arrays.az[pidx];
				
			force_handle.data[body].x += fx;
			force_handle.data[body].y += fy;
			force_handle.data[body].z += fz;
			
			// Torque = r x f (all are in the space frame)
			Scalar rx = arrays.x[pidx] - com_handle.data[body].x;
			Scalar ry = arrays.y[pidx] - com_handle.data[body].y;
			Scalar rz = arrays.z[pidx] - com_handle.data[body].z;
				
			if (rx >= box.xhi) rx -= Lx;
			if (rx < box.xlo) rx += Lx;
			if (ry >= box.yhi) ry -= Ly;
			if (ry < box.ylo) ry += Ly;
			if (rz >= box.zhi) rz -= Lz;
			if (rz < box.zlo) rz += Lz;
				
			torque_handle.data[body].x += ry * fz - rz * fy;
			torque_handle.data[body].y += rz * fx - rx * fz;
			torque_handle.data[body].z += rx * fy - ry * fx;
			
			// Angular momentum = r x (m * v) is calculated for setup
			angmom_handle.data[body].x += ry * (mass_one * arrays.vz[pidx]) - rz * (mass_one * arrays.vy[pidx]);
			angmom_handle.data[body].y += rz * (mass_one * arrays.vx[pidx]) - rx * (mass_one * arrays.vz[pidx]);
			angmom_handle.data[body].z += rx * (mass_one * arrays.vy[pidx]) - ry * (mass_one * arrays.vx[pidx]);
			}
		}
	
	m_pdata->release();
	
	} // out of scope for handles
		
	// Set the velocities of particles in rigid bodies
	set_v();
	}

/*! Velocity verlet: adapted from LAMMPS (Large-scale Atomistic/Molecular Massively Parallel Simulator)

*/
void NVERigidUpdater::initialIntegrate()
	{	
	// get box
	const BoxDim& box = m_pdata->getBox();
	// sanity check
	assert(box.xhi > box.xlo && box.yhi > box.ylo && box.zhi > box.zlo);	
		
	// precalculate box lenghts
	Scalar Lx = box.xhi - box.xlo;
	Scalar Ly = box.yhi - box.ylo;
	Scalar Lz = box.zhi - box.zlo;
		
	// now we can get on with the velocity verlet: initial integration
	Scalar dt_half = 0.5 * m_deltaT;
	Scalar dtfm;
	
	{ 
	// rigid data handles
	ArrayHandle<Scalar> body_mass_handle(m_rigid_data->getBodyMass(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> moment_inertia_handle(m_rigid_data->getMomentInertia(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> force_handle(m_force, access_location::host, access_mode::read);
	ArrayHandle<Scalar4> torque_handle(m_torque, access_location::host, access_mode::read);
		
	ArrayHandle<Scalar4> com_handle(m_rigid_data->getCOM(), access_location::host, access_mode::readwrite);	
	ArrayHandle<Scalar4> vel_handle(m_rigid_data->getVel(), access_location::host, access_mode::readwrite);
	ArrayHandle<Scalar4> orientation_handle(m_rigid_data->getOrientation(), access_location::host, access_mode::readwrite);	
	ArrayHandle<Scalar4> angmom_handle(m_rigid_data->getAngMom(), access_location::host, access_mode::readwrite);
	ArrayHandle<Scalar4> angvel_handle(m_rigid_data->getAngVel(), access_location::host, access_mode::readwrite);
	
	ArrayHandle<unsigned int> body_imagex_handle(m_rigid_data->getBodyImagex(), access_location::host, access_mode::readwrite);	
	ArrayHandle<unsigned int> body_imagey_handle(m_rigid_data->getBodyImagey(), access_location::host, access_mode::readwrite);	
	ArrayHandle<unsigned int> body_imagez_handle(m_rigid_data->getBodyImagez(), access_location::host, access_mode::readwrite);	
	ArrayHandle<Scalar4> ex_space_handle(m_rigid_data->getExSpace(), access_location::host, access_mode::readwrite);
	ArrayHandle<Scalar4> ey_space_handle(m_rigid_data->getEySpace(), access_location::host, access_mode::readwrite);
	ArrayHandle<Scalar4> ez_space_handle(m_rigid_data->getEzSpace(), access_location::host, access_mode::readwrite);		
	
	// for each body
	for (unsigned int body = 0; body < m_n_bodies; body++) 
		{
		dtfm = dt_half / body_mass_handle.data[body];	
		vel_handle.data[body].x += dtfm * force_handle.data[body].x;
		vel_handle.data[body].y += dtfm * force_handle.data[body].y;
		vel_handle.data[body].z += dtfm * force_handle.data[body].z;
				
		com_handle.data[body].x += vel_handle.data[body].x * m_deltaT;
		com_handle.data[body].y += vel_handle.data[body].y * m_deltaT;
		com_handle.data[body].z += vel_handle.data[body].z * m_deltaT;
		
		// map the center of mass to the periodic box, update the com image info
		if (com_handle.data[body].x >= box.xhi) 
			{
			com_handle.data[body].x -= Lx;
			body_imagex_handle.data[body]--;
			}
		else if (com_handle.data[body].x < box.xlo) 
			{
			com_handle.data[body].x += Lx;
			body_imagex_handle.data[body]++;
			}	
		
		if (com_handle.data[body].y >= box.yhi) 
			{
			com_handle.data[body].y -= Ly;
			body_imagey_handle.data[body]--;
			}
		else if (com_handle.data[body].y < box.ylo) 
			{
			com_handle.data[body].y += Ly;
			body_imagey_handle.data[body]++;
			}
		
		if (com_handle.data[body].z >= box.zhi) 
			{
			com_handle.data[body].z -= Lz;
			body_imagez_handle.data[body]--;
			}
		else if (com_handle.data[body].z < box.zlo) 
			{
			com_handle.data[body].z += Lz;
			body_imagez_handle.data[body]++;
			}
			
		angmom_handle.data[body].x += dt_half * torque_handle.data[body].x;
		angmom_handle.data[body].y += dt_half * torque_handle.data[body].y;
		angmom_handle.data[body].z += dt_half * torque_handle.data[body].z;
				
		advanceQuaternion(angmom_handle.data[body], moment_inertia_handle.data[body], angvel_handle.data[body], 
						ex_space_handle.data[body], ey_space_handle.data[body], ez_space_handle.data[body], orientation_handle.data[body]);
		}
	} // out of scope for handles
	
	// set positions and velocities of particles in rigid bodies	
	set_xv();

	}

void NVERigidUpdater::finalIntegrate()
{			
	// compute net forces and torques on rigid bodies from particle forces 
	computeForceAndTorque();
	
	{
	// rigid data handes
	ArrayHandle<Scalar> body_mass_handle(m_rigid_data->getBodyMass(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> moment_inertia_handle(m_rigid_data->getMomentInertia(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> ex_space_handle(m_rigid_data->getExSpace(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> ey_space_handle(m_rigid_data->getEySpace(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> ez_space_handle(m_rigid_data->getEzSpace(), access_location::host, access_mode::read);
	
	ArrayHandle<Scalar4> force_handle(m_force, access_location::host, access_mode::read);
	ArrayHandle<Scalar4> torque_handle(m_torque, access_location::host, access_mode::read);
	
	ArrayHandle<Scalar4> vel_handle(m_rigid_data->getVel(), access_location::host, access_mode::readwrite);
	ArrayHandle<Scalar4> angmom_handle(m_rigid_data->getAngMom(), access_location::host, access_mode::readwrite);
	ArrayHandle<Scalar4> angvel_handle(m_rigid_data->getAngVel(), access_location::host, access_mode::readwrite);

	
	Scalar dt_half = 0.5 * m_deltaT;
	
	// 2nd step: final integration
	for (unsigned int body = 0; body < m_n_bodies; body++)
		{
		Scalar dtfm = dt_half / body_mass_handle.data[body];	
		vel_handle.data[body].x += dtfm * force_handle.data[body].x;
		vel_handle.data[body].y += dtfm * force_handle.data[body].y;
		vel_handle.data[body].z += dtfm * force_handle.data[body].z;
			
		angmom_handle.data[body].x += dt_half * torque_handle.data[body].x;
		angmom_handle.data[body].y += dt_half * torque_handle.data[body].y;
		angmom_handle.data[body].z += dt_half * torque_handle.data[body].z;
		
		computeAngularVelocity(angmom_handle.data[body], moment_inertia_handle.data[body],
							   ex_space_handle.data[body], ey_space_handle.data[body], ez_space_handle.data[body], angvel_handle.data[body]);	
		}
	} // out of scope for handles	
	
	// set velocities of particles in rigid bodies	
	set_v();

	}


void NVERigidUpdater::computeForceAndTorque()
	{
	// get box
	const BoxDim& box = m_pdata->getBox();
	// sanity check
	assert(box.xhi > box.xlo && box.yhi > box.ylo && box.zhi > box.zlo);	
		
	// precalculate box lenghts
	Scalar Lx = box.xhi - box.xlo;
	Scalar Ly = box.yhi - box.ylo;
	Scalar Lz = box.zhi - box.zlo;
	
	// rigid data handles
	ArrayHandle<unsigned int> body_size_handle(m_rigid_data->getBodySize(), access_location::host, access_mode::read);	
	ArrayHandle<Scalar4> com_handle(m_rigid_data->getCOM(), access_location::host, access_mode::read);	
	ArrayHandle<unsigned int> particle_indices_handle(m_rigid_data->getParticleIndices(), access_location::host, access_mode::read);
	unsigned int indices_pitch = m_rigid_data->getParticleIndices().getPitch();	
		
	ArrayHandle<Scalar4> force_handle(m_force, access_location::host, access_mode::readwrite);
	ArrayHandle<Scalar4> torque_handle(m_torque, access_location::host, access_mode::readwrite);
		
	// reset all forces and torques
	for (unsigned int body = 0; body < m_n_bodies; body++)
		{
		force_handle.data[body].x = 0.0;
		force_handle.data[body].y = 0.0;
		force_handle.data[body].z = 0.0;
		
		torque_handle.data[body].x = 0.0;
		torque_handle.data[body].y = 0.0;
		torque_handle.data[body].z = 0.0;
		}
	
	// access the particle data arrays
	const ParticleDataArraysConst &arrays = m_pdata->acquireReadOnly();
	assert(arrays.x != NULL && arrays.y != NULL && arrays.z != NULL);
	assert(arrays.ax != NULL && arrays.ay != NULL && arrays.az != NULL);	
	
	// for each body
	for (unsigned int body = 0; body < m_n_bodies; body++)
		{	
		// for each particle
		unsigned int len = body_size_handle.data[body];
		for (unsigned int j = 0; j < len; j++)
			{
			// get the actual index of particle in the particle arrays
			unsigned int pidx = particle_indices_handle.data[body * indices_pitch + j];
			
			// get the particle mass 
			Scalar mass_one = arrays.mass[pidx];
			
			Scalar fx = mass_one * arrays.ax[pidx];
			Scalar fy = mass_one * arrays.ay[pidx];
			Scalar fz = mass_one * arrays.az[pidx];
			
			force_handle.data[body].x += fx;
			force_handle.data[body].y += fy;
			force_handle.data[body].z += fz;
			
			// torque = r x f
			Scalar rx = arrays.x[pidx] - com_handle.data[body].x;
			Scalar ry = arrays.y[pidx] - com_handle.data[body].y;
			Scalar rz = arrays.z[pidx] - com_handle.data[body].z;
			
			if (rx >= box.xhi) rx -= Lx;
			if (rx < box.xlo) rx += Lx;
			if (ry >= box.yhi) ry -= Ly;
			if (ry < box.ylo) ry += Ly;
			if (rz >= box.zhi) rz -= Lz;
			if (rz < box.zlo) rz += Lz;
			
			torque_handle.data[body].x += ry * fz - rz * fy;
			torque_handle.data[body].y += rz * fx - rx * fz;
			torque_handle.data[body].z += rx * fy - ry * fx;
			}
		}
	
	m_pdata->release();
		
	}

/*! Set position and velocity of constituent particles in rigid bodies in the 1st half of integration
	based on the body center of mass and particle relative position in each body frame.
  
	particle_index gives the index of the particle in the GPU array particle_pos which is of (nmax by nbodies)
*/

void NVERigidUpdater::set_xv()
{
	// get box	
	const BoxDim& box = m_pdata->getBox();
	// sanity check
	assert(box.xhi > box.xlo && box.yhi > box.ylo && box.zhi > box.zlo);	
	
	// handles
	ArrayHandle<unsigned int> body_size_handle(m_rigid_data->getBodySize(), access_location::host, access_mode::read);	
	ArrayHandle<Scalar4> com(m_rigid_data->getCOM(), access_location::host, access_mode::read);	
	ArrayHandle<Scalar4> vel_handle(m_rigid_data->getVel(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> angvel_handle(m_rigid_data->getAngVel(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> ex_space_handle(m_rigid_data->getExSpace(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> ey_space_handle(m_rigid_data->getEySpace(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> ez_space_handle(m_rigid_data->getEzSpace(), access_location::host, access_mode::read);
	
	ArrayHandle<unsigned int> particle_indices_handle(m_rigid_data->getParticleIndices(), access_location::host, access_mode::read);
	unsigned int indices_pitch = m_rigid_data->getParticleIndices().getPitch();	
	ArrayHandle<Scalar4> particle_pos_handle(m_rigid_data->getParticlePos(), access_location::host, access_mode::read);
	unsigned int particle_pos_pitch = m_rigid_data->getParticlePos().getPitch();	
	
	// access the particle data arrays
	ParticleDataArrays arrays = m_pdata->acquireReadWrite();
	assert(arrays.x != NULL && arrays.y != NULL && arrays.z != NULL);
	assert(arrays.vx != NULL && arrays.vy != NULL && arrays.vz != NULL);
	assert(arrays.ix != NULL && arrays.iy != NULL && arrays.iz != NULL);
	
	// for each body
	for (unsigned int body = 0; body < m_n_bodies; body++) 
		{
		unsigned int len = body_size_handle.data[body];
		// for each particle
		for (unsigned int j = 0; j < len; j++) 
			{
			// get the actual index of particle in the particle arrays
			unsigned int pidx = particle_indices_handle.data[body * indices_pitch + j];
			// get the index of particle in the current rigid body in the particle_pos array
			unsigned int localidx = body * particle_pos_pitch + j;
				
			// project the position in the body frame to the space frame: xr = rotation_matrix * particle_pos
			Scalar xr = ex_space_handle.data[body].x * particle_pos_handle.data[localidx].x 
					+ ey_space_handle.data[body].x * particle_pos_handle.data[localidx].y 
					+ ez_space_handle.data[body].x * particle_pos_handle.data[localidx].z;
			Scalar yr = ex_space_handle.data[body].y * particle_pos_handle.data[localidx].x 
					+ ey_space_handle.data[body].y * particle_pos_handle.data[localidx].y 
					+ ez_space_handle.data[body].y * particle_pos_handle.data[localidx].z;
			Scalar zr = ex_space_handle.data[body].z * particle_pos_handle.data[localidx].x 
					+ ey_space_handle.data[body].z * particle_pos_handle.data[localidx].y 
					+ ez_space_handle.data[body].z * particle_pos_handle.data[localidx].z;
			
			// x_particle = x_com + xr
			arrays.x[pidx] = com.data[body].x + xr;
			arrays.y[pidx] = com.data[body].y + yr;
			arrays.z[pidx] = com.data[body].z + zr;
			
			//	Let the caller (NVEUpdater) do the particle wrap-up
			
			// v_particle = v_com + angvel x xr
			arrays.vx[pidx] = vel_handle.data[body].x + angvel_handle.data[body].y * zr - angvel_handle.data[body].z * yr;
			arrays.vy[pidx] = vel_handle.data[body].y + angvel_handle.data[body].z * xr - angvel_handle.data[body].x * zr;
			arrays.vz[pidx] = vel_handle.data[body].z + angvel_handle.data[body].x * yr - angvel_handle.data[body].y * xr;

			}
		}
	
	m_pdata->release();
	
	}

/*! Set velocity of constituent particles in rigid bodies in the 2nd half of integration
 based on the body center of mass and particle relative position in each body frame.
 
 */

void NVERigidUpdater::set_v()
	{
	// rigid data handles
	ArrayHandle<unsigned int> body_size_handle(m_rigid_data->getBodySize(), access_location::host, access_mode::read);	
	ArrayHandle<Scalar4> vel_handle(m_rigid_data->getVel(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> angvel_handle(m_rigid_data->getAngVel(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> ex_space_handle(m_rigid_data->getExSpace(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> ey_space_handle(m_rigid_data->getEySpace(), access_location::host, access_mode::read);
	ArrayHandle<Scalar4> ez_space_handle(m_rigid_data->getEzSpace(), access_location::host, access_mode::read);
	
	ArrayHandle<unsigned int> particle_indices_handle(m_rigid_data->getParticleIndices(), access_location::host, access_mode::read);
	unsigned int indices_pitch = m_rigid_data->getParticleIndices().getPitch();	
	ArrayHandle<Scalar4> particle_pos_handle(m_rigid_data->getParticlePos(), access_location::host, access_mode::read);
	unsigned int particle_pos_pitch = m_rigid_data->getParticlePos().getPitch();
				
	// access the particle data arrays
	ParticleDataArrays arrays = m_pdata->acquireReadWrite();
	assert(arrays.vx != NULL && arrays.vy != NULL && arrays.vz != NULL);
	
	// for each body
	for (unsigned int body = 0; body < m_n_bodies; body++) 
		{
		unsigned int len = body_size_handle.data[body];
		// for each particle
		for (unsigned int j = 0; j < len; j++) 
			{
			// get the actual index of particle in the particle arrays
			unsigned int pidx = particle_indices_handle.data[body * indices_pitch + j];
			// get the index of particle in the current rigid body in the particle_pos array
			unsigned int localidx = body * particle_pos_pitch + j;
				
			// project the position in the body frame to the space frame: xr = rotation_matrix * particle_pos
			Scalar xr = ex_space_handle.data[body].x * particle_pos_handle.data[localidx].x 
						+ ey_space_handle.data[body].x * particle_pos_handle.data[localidx].y 
						+ ez_space_handle.data[body].x * particle_pos_handle.data[localidx].z;
			Scalar yr = ex_space_handle.data[body].y * particle_pos_handle.data[localidx].x 
						+ ey_space_handle.data[body].y * particle_pos_handle.data[localidx].y 
						+ ez_space_handle.data[body].y * particle_pos_handle.data[localidx].z;
			Scalar zr = ex_space_handle.data[body].z * particle_pos_handle.data[localidx].x 
						+ ey_space_handle.data[body].z * particle_pos_handle.data[localidx].y 
						+ ez_space_handle.data[body].z * particle_pos_handle.data[localidx].z;
			
			// v_particle = v_com + angvel x xr
			arrays.vx[pidx] = vel_handle.data[body].x + angvel_handle.data[body].y * zr - angvel_handle.data[body].z * yr;
			arrays.vy[pidx] = vel_handle.data[body].y + angvel_handle.data[body].z * xr - angvel_handle.data[body].x * zr;
			arrays.vz[pidx] = vel_handle.data[body].z + angvel_handle.data[body].x * yr - angvel_handle.data[body].y * xr;
			}
		}
	
	m_pdata->release();
		
	}

/*! Update orientation (ex_space, ey_space, ez_space) from quaternion.
 
 */

void NVERigidUpdater::exyzFromQuaternion(Scalar4 &quat, Scalar4 &ex_space, Scalar4 &ey_space, Scalar4 &ez_space)
	{
	// ex_space
	ex_space.x = quat.x * quat.x + quat.y * quat.y - quat.z * quat.z - quat.w * quat.w;
	ex_space.y = 2.0 * (quat.y * quat.z + quat.x * quat.w);
	ex_space.z = 2.0 * (quat.y * quat.w - quat.x * quat.z);
	
	// ey_space
	ey_space.x = 2.0 * (quat.y * quat.z - quat.x * quat.w);
	ey_space.y = quat.x * quat.x - quat.y * quat.y + quat.z * quat.z - quat.w * quat.w;
	ey_space.z = 2.0 * (quat.z * quat.w + quat.x * quat.y);
	
	// ez_space
	ez_space.x = 2.0 * (quat.y * quat.w + quat.x * quat.z);
	ez_space.y = 2.0 * (quat.z * quat.w - quat.x * quat.y);
	ez_space.z = quat.x * quat.x - quat.y * quat.y - quat.z * quat.z + quat.w * quat.w;
	}

/*! Compute angular velocity from angular momentum. 
	Convert the angular momentum from world frame to body frame.
	Compute angular velocity in the body frame (angbody).
	Convert the angular velocity from body frame back to world frame.
 
	Rotation matrix is formed by arranging ex_space, ey_space and ez_space vectors into columns.
	In this code, rotation matrix is used to map a vector in a body frame into the space frame: 
		x_space = rotation_matrix * x_body
	The reverse operation is to convert a vector in the space frame to a body frame:
		x_body = transpose(rotation matrix) * x_space
 */

void NVERigidUpdater::computeAngularVelocity(Scalar4& angmom, Scalar4& moment_inertia, Scalar4& ex_space, Scalar4& ey_space, Scalar4& ez_space,
											Scalar4& angvel)
	{
	//! Angular velocity in the body frame
	Scalar angbody[3];
		
	//! angbody = angmom_body / moment_inertia = transpose(rotation_matrix) * angmom / moment_inertia  
	if (moment_inertia.x == 0.0) angbody[0] = 0.0;
	else angbody[0] = (ex_space.x * angmom.x + ex_space.y * angmom.y 
						  + ex_space.z * angmom.z) / moment_inertia.x;
	
	if (moment_inertia.y == 0.0) angbody[1] = 0.0;
	else angbody[1] = (ey_space.x * angmom.x + ey_space.y * angmom.y
						+ ey_space.z * angmom.z) / moment_inertia.y;
	
	if (moment_inertia.z == 0.0) angbody[2] = 0.0;
	else angbody[2] = (ez_space.x * angmom.x + ez_space.y * angmom.y 
						+ ez_space.z * angmom.z) / moment_inertia.z;
	
	//! Convert to angbody to the space frame: angvel = rotation_matrix * angbody
	angvel.x = angbody[0] * ex_space.x + angbody[1] * ey_space.x + angbody[2] * ez_space.x;
	angvel.y = angbody[0] * ex_space.y + angbody[1] * ey_space.y + angbody[2] * ez_space.y;
	angvel.z = angbody[0] * ex_space.z + angbody[1] * ey_space.z + angbody[2] * ez_space.z;
	}

/*! Advance the quaternion using angular momentum and angular velocity
 */
void NVERigidUpdater::advanceQuaternion(Scalar4& angmom, Scalar4 &moment_inertia, Scalar4 &angvel, 
										Scalar4& ex_space, Scalar4& ey_space, Scalar4& ez_space, Scalar4 &quat)
	{
	Scalar4 qhalf, qfull, omegaq;
	Scalar dtq = 0.5 * m_deltaT;
		
	computeAngularVelocity(angmom, moment_inertia, ex_space, ey_space, ez_space, angvel);
	
	// Compute (w q)
	multiply(angvel, quat, omegaq);
	
	// Full update q from dq/dt = 1/2 w q
	qfull.x = quat.x + dtq * omegaq.x;
	qfull.y = quat.y + dtq * omegaq.y;
	qfull.z = quat.z + dtq * omegaq.z;
	qfull.w = quat.w + dtq * omegaq.w;
	normalize(qfull);
	
	// 1st half update from dq/dt = 1/2 w q
	qhalf.x = quat.x + 0.5 * dtq * omegaq.x;
	qhalf.y = quat.y + 0.5 * dtq * omegaq.y;
	qhalf.z = quat.z + 0.5 * dtq * omegaq.z;
	qhalf.w = quat.w + 0.5 * dtq * omegaq.w;
	normalize(qhalf);
	
	// Udpate ex, ey, ez from qhalf = update A
	exyzFromQuaternion(qhalf, ex_space, ey_space, ez_space);
	
	// Compute angular velocity from new ex_space, ey_space and ex_space
	computeAngularVelocity(angmom, moment_inertia, ex_space, ey_space, ez_space, angvel);
	
	// Compute (w qhalf)
	multiply(angvel, qhalf, omegaq);
	
	// 2nd half update from dq/dt = 1/2 w q
	qhalf.x += 0.5 * dtq * omegaq.x;
	qhalf.y += 0.5 * dtq * omegaq.y;
	qhalf.z += 0.5 * dtq * omegaq.z;
	qhalf.w += 0.5 * dtq * omegaq.w;
	normalize(qhalf);
	
	// Corrected Richardson update
	quat.x = 2.0 * qhalf.x - qfull.x;
	quat.y = 2.0 * qhalf.y - qfull.y;
	quat.z = 2.0 * qhalf.z - qfull.z;
	quat.w = 2.0 * qhalf.w - qfull.w;
	normalize(quat);
	
	exyzFromQuaternion(quat, ex_space, ey_space, ez_space);
	}

/*! Quaternion multiply: c = a * b where a = (0, a)
 */

void NVERigidUpdater::multiply(Scalar4 &a, Scalar4 &b, Scalar4 &c)
	{
	c.x = -(a.x * b.y + a.y * b.z + a.z * b.w);
	c.y =   b.x * a.x + a.y * b.w - a.z * b.z;
	c.z =   b.x * a.y + a.z * b.y - a.x * b.w;
	c.w =   b.x * a.z + a.x * b.z - a.y * b.y;
	}

/*! Normalize a quaternion
 */

void NVERigidUpdater::normalize(Scalar4 &q)
	{
	Scalar norm = 1.0 / sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	q.x *= norm;
	q.y *= norm;
	q.z *= norm;
	q.w *= norm;
	}


#ifdef WIN32
#pragma warning( pop )
#endif