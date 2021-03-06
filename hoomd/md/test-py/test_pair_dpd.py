# -*- coding: iso-8859-1 -*-
# Maintainer: joaander

from hoomd import *
from hoomd import deprecated
from hoomd import md;
context.initialize()
import unittest
import os

# md.pair.dpd
class pair_dpd_tests (unittest.TestCase):
    def setUp(self):
        print
        deprecated.init.create_random(N=100, phi_p=0.05);
        self.nl = md.nlist.cell()
        context.current.sorter.set_params(grid=8)

    # basic test of creation
    def test(self):
        dpd = md.pair.dpd(r_cut=3.0, nlist = self.nl, kT=1.0, seed=10);
        dpd.pair_coeff.set('A', 'A', A=1.0, gamma = 4.5, r_cut=2.5);
        dpd.update_coeffs();

    # test missing coefficients
    def test_set_missing_gamma(self):
        dpd = md.pair.dpd(r_cut=3.0, nlist = self.nl, kT=1.0, seed=12);
        dpd.pair_coeff.set('A', 'A', A=40);
        self.assertRaises(RuntimeError, dpd.update_coeffs);

    # test missing coefficients
    def test_missing_AA(self):
        dpd = md.pair.dpd(r_cut=3.0, nlist = self.nl, kT=1.0, seed=16);
        self.assertRaises(RuntimeError, dpd.update_coeffs);

    def tearDown(self):
        del self.nl
        context.initialize();


if __name__ == '__main__':
    unittest.main(argv = ['test.py', '-v'])
