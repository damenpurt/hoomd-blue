# Copyright (c) 2009-2016 The Regents of the University of Michigan
# This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

# Maintainer: joaander / All Developers are free to add commands for new features

R""" Update particle properties.

When an updater is specified, it acts on the particle system each time step to change
it in some way. See the documentation of specific updaters to find out what they do.
"""

from hoomd import _hoomd
from hoomd.md import _md
import hoomd;
from hoomd.update import _updater
import sys;

class rescale_temp(_updater):
    r""" Rescales particle velocities.

    Args:
        kT (:py:mod:`hoomd.variant` or :py:obj:`float`): Temperature set point (in energy units)
        period (int): Velocities will be rescaled every *period* time steps
        phase (int): When -1, start on the current time step. When >= 0, execute on steps where *(step + phase) % period == 0*.

    Every *period* time steps, particle velocities and angular momenta are rescaled by equal factors
    so that they are consistent with a given temperature in the equipartition theorem

    .. math::

        \langle 1/2 m v^2 \rangle = k_B T

        \langle 1/2 I \omega^2 \rangle = k_B T

    .. attention::
        :py:class:`rescale_temp` does **not** run on the GPU, and will significantly slow down simulations.

    Examples::

        update.rescale_temp(kT=1.2)
        rescaler = update.rescale_temp(kT=0.5)
        update.rescale_temp(period=100, kT=1.03)
        update.rescale_temp(period=100, kT=hoomd.variant.linear_interp([(0, 4.0), (1e6, 1.0)]))

    """
    def __init__(self, kT, period=1, phase=0):
        hoomd.util.print_status_line();

        # initialize base class
        _updater.__init__(self);

        # setup the variant inputs
        kT = hoomd.variant._setup_variant_input(kT);

        # create the compute thermo
        thermo = hoomd.compute._get_unique_thermo(group=hoomd.context.current.group_all);

        # create the c++ mirror class
        self.cpp_updater = _md.TempRescaleUpdater(hoomd.context.current.system_definition, thermo.cpp_compute, kT.cpp_variant);
        self.setupUpdater(period, phase);

        # store metadta
        self.kT = kT
        self.period = period
        self.metadata_fields = ['kT','period']

    def set_params(self, kT=None):
        R""" Change rescale_temp parameters.

        Args:
            kT (:py:mod:`hoomd.variant` or :py:obj:`float`): New temperature set point (in energy units)

        Examples::

            rescaler.set_params(kT=2.0)

        """
        hoomd.util.print_status_line();
        self.check_initialization();

        if kT is not None:
            kT = hoomd.variant._setup_variant_input(kT);
            self.cpp_updater.setT(kT.cpp_variant);
            self.kT = kT

class zero_momentum(_updater):
    R""" Zeroes system momentum.

    Args:
        period (int): Momentum will be zeroed every *period* time steps
        phase (int): When -1, start on the current time step. When >= 0, execute on steps where *(step + phase) % period == 0*.

    Every *period* time steps, particle velocities are modified such that the total linear
    momentum of the system is set to zero.

    Examples::

        update.zero_momentum()
        zeroer= update.zero_momentum(period=10)

    """
    def __init__(self, period=1, phase=0):
        hoomd.util.print_status_line();

        # initialize base class
        _updater.__init__(self);

        # create the c++ mirror class
        self.cpp_updater = _md.ZeroMomentumUpdater(hoomd.context.current.system_definition);
        self.setupUpdater(period, phase);

        # store metadata
        self.period = period
        self.metadata_fields = ['period']

class enforce2d(_updater):
    R""" Enforces 2D simulation.

    Every time step, particle velocities and accelerations are modified so that their z components are 0: forcing
    2D simulations when other calculations may cause particles to drift out of the plane. Using enforce2d is only
    allowed when the system is specified as having only 2 dimensions.

    Examples::

        update.enforce2d()

    """
    def __init__(self):
        hoomd.util.print_status_line();
        period = 1;

        # initialize base class
        _updater.__init__(self);

        # create the c++ mirror class
        if not hoomd.context.exec_conf.isCUDAEnabled():
            self.cpp_updater = _md.Enforce2DUpdater(hoomd.context.current.system_definition);
        else:
            self.cpp_updater = _md.Enforce2DUpdaterGPU(hoomd.context.current.system_definition);
        self.setupUpdater(period);

class constraint_ellipsoid(_updater):
    R""" Constrain particles to the surface of a ellipsoid.

    Args:
        group (:py:mod:`hoomd.group`): Group for which the update will be set
        P (tuple): (x,y,z) tuple indicating the position of the center of the ellipsoid (in distance units).
        rx (float): radius of an ellipsoid in the X direction (in distance units).
        ry (float): radius of an ellipsoid in the Y direction (in distance units).
        rz (float): radius of an ellipsoid in the Z direction (in distance units).
        r (float): radius of a sphere (in distance units), such that r=rx=ry=rz.

    :py:class:`constraint_ellipsoid` specifies that all particles are constrained
    to the surface of an ellipsoid. Each time step particles are projected onto the surface of the ellipsoid.
    Method from: http://www.geometrictools.com/Documentation/DistancePointEllipseEllipsoid.pdf

    .. attention::
        For the algorithm to work, we must have :math:`rx >= rz,~ry >= rz,~rz > 0`.

    Note:
        This method does not properly conserve virial coefficients.

    Note:
        random thermal forces from the integrator are applied in 3D not 2D, therefore they aren't fully accurate.
        Suggested use is therefore only for T=0.

    Examples::

        update.constraint_ellipsoid(P=(-1,5,0), r=9)
        update.constraint_ellipsoid(rx=7, ry=5, rz=3)

    """
    def __init__(self, group, r=None, rx=None, ry=None, rz=None, P=(0,0,0)):
        hoomd.util.print_status_line();
        period = 1;

        # Error out in MPI simulations
        if (_hoomd.is_MPI_available()):
            if context.current.system_definition.getParticleData().getDomainDecomposition():
                context.msg.error("constrain.ellipsoid is not supported in multi-processor simulations.\n\n")
                raise RuntimeError("Error initializing updater.")

        # Error out if no radii are set
        if (r is None and rx is None and ry is None and rz is None):
            context.msg.error("no radii were defined in update.constraint_ellipsoid.\n\n")
            raise RuntimeError("Error initializing updater.")

        # initialize the base class
        _updater.__init__(self);

        # Set parameters
        P = _hoomd.make_scalar3(P[0], P[1], P[2]);
        if (r is not None): rx = ry = rz = r

        # create the c++ mirror class
        if not hoomd.context.exec_conf.isCUDAEnabled():
            self.cpp_updater = _md.ConstraintEllipsoid(hoomd.context.current.system_definition, group.cpp_group, P, rx, ry, rz);
        else:
            self.cpp_updater = _md.ConstraintEllipsoidGPU(hoomd.context.current.system_definition, group.cpp_group, P, rx, ry, rz);

        self.setupUpdater(period);

        # store metadata
        self.group = group
        self.P = P
        self.rx = rx
        self.ry = ry
        self.rz = rz
        self.metadata_fields = ['group','P', 'rx', 'ry', 'rz']
