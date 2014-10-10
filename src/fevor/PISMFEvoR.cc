/* Copyright (C) 2014 PISM Authors
 *
 * This file is part of PISM.
 *
 * PISM is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * PISM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PISM; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <cassert>
#include <vector>

#include <fevor_distribution.hh>
#include <vector_tensor_operations.hh>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Delaunay_triangulation_2.h>
#include <CGAL/Interpolation_traits_2.h>
#include <CGAL/natural_neighbor_coordinates_2.h>
#include <CGAL/interpolation_functions.h>

#include "PISMFEvoR.hh"
#include "PISMConfig.hh"
#include "PISMVars.hh"
#include "PISMStressBalance_diagnostics.hh"
#include "enthalpyConverter.hh"

namespace pism {

PISMFEvoR::PISMFEvoR(IceGrid &g, const Config &conf, EnthalpyConverter *EC,
                     StressBalance *stress_balance)
  : Component_TS(g, conf), m_stress_balance(stress_balance), m_EC(EC) {

  assert(m_EC != NULL);
  assert(m_stress_balance != NULL);

  PetscErrorCode ierr = allocate(); CHKERRCONTINUE(ierr);

  // will be allocated in init()
  m_pressure = NULL;
  m_tauxz    = NULL;
  m_tauyz    = NULL;

  // points to storage owned by IceModel
  m_enthalpy = NULL;
}

PISMFEvoR::~PISMFEvoR() {
  delete m_pressure;
  delete m_tauxz;
  delete m_tauyz;
}

PetscErrorCode PISMFEvoR::max_timestep(double t, double &dt, bool &restrict) {
  // FIXME: put real code here
  PetscErrorCode ierr = Component_TS::max_timestep(t, dt, restrict); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode PISMFEvoR::update(double t, double dt) {
  m_t = t;
  m_dt = dt;
  PetscErrorCode ierr;

  IceModelVec3 *u = NULL, *v = NULL, *w = NULL;
  ierr = m_stress_balance->get_3d_velocity(u, v, w); CHKERRQ(ierr);

  assert(m_pressure != NULL);
  IceModelVec* pressure = NULL; // FIXME: use a smart pointer
  ierr = m_pressure->compute(pressure); CHKERRQ(ierr);
  IceModelVec3 *pressure3 = static_cast<IceModelVec3*>(pressure);

  assert(m_tauxz != NULL);
  IceModelVec* tauxz = NULL;    // FIXME: use a smart pointer
  ierr = m_tauxz->compute(tauxz); CHKERRQ(ierr);
  IceModelVec3 *tauxz3 = static_cast<IceModelVec3*>(tauxz);

  assert(m_tauyz != NULL);
  IceModelVec* tauyz = NULL;    // FIXME: use a smart pointer
  ierr = m_tauyz->compute(tauyz); CHKERRQ(ierr);
  IceModelVec3 *tauyz3 = static_cast<IceModelVec3*>(tauyz);
  

  {
    ierr = m_enhancement_factor.set(config.get("sia_enhancement_factor")); CHKERRQ(ierr);

    unsigned int n_particles = 10;
    double n_pd = double(n_particles);
      /* terminology:
       *   particles exist in pism and contain one or more distrobutions 
       *   of crystals that are tracked through time. They essentially are 
       *   infinitesimely small. Distributions exist in FEvoR and contain 
       *   sets of independent crystals (or in the case of NNI weakly 
       *   dependant). In PISM-FEvoR you will likely not need to access the
       *   crystals directly. Methods should be provided through FEvoR's 
       *   distribution class fevor_distribution. 
       */
      
    /* TODO method to load in our cloud of particles. Will need the values
     * below for each particle.
     * 
     * Just making a fake one:
     */
      std::vector<unsigned int> packingDimensions(3, 3); 
        /* This should be the same for each distribution, but a loaded parameter.
         * 
         * Also, should be at minimum 10x10x10 to get an accurate result! low here for testing
         */
      std::vector<double> p_x, p_y, p_z, p_e;
      for (double pn=0.0; pn < n_pd; ++pn) {
        px_x.push_back(pn < n_pd/2.0 ? -grid.Lx+ 2.0*grid.Lx*(2*pn/n_pd)
                                     : -grid.Lx+ 2.0*grid.Lx*(2*pn/n_pd-1)) );
        p_y.push_back(0.0);
        p_z.push_back(pn < n_pd/2.0 ? 0.0 : grid.Lz );
        
        p_e.push_back(1.0);
      }
      // Diagnostics -- total number of recrystallization events in time step
      std::vector<unsigned int> nMigre(n_particles, 0), nPoly(n_particles, 0);    
    
    // get enhancement factor for every particle!
    for (unsigned int i = 0; i < n_particles; ++i) {
      /* FIXME this should get the appropriate distribuion! 
       * 
       * Just create one from a Watson concentration parameter. 
       */
      double w_i = -3.0; // This makes a weak bi-polar (single maximum)
      fevor_distribution d_i(packingDimensions, w_i);
      
      /* Make an isotropic distribution for calculating enhancement factor. The 
       * enhancement factor is defined as ratio of ice's resonse relative to 
       * isotropic ice. Since we need isotropic ice's responce to any input
       * stress, this is the easiest way to provide it but it may be the most 
       * computationally heavy. Possible efficienty improvement here. 
       */
      fevor_distribution d_iso(packingDimensions, 0.0);
      
      std::vector<double> bulkEdot(9, 0.0);
      
      unsigned int nMigRe_iso = 0,
                   nPoly_iso  = 0;
      std::vector<double> bulkEdot_iso(9, 0.0);
             
      
      // interpolate these values from PISM
      double P   = 0.0, 
             txz = 0.0,
             tyz = 0.0;
             
      double E = 0.0,
             T = 0.0;

      // check if the point (x,y,z) is within the domain:
      assert(     0.0 <= p_z[i] && p_z[i] <= grid.Lz);
      assert(-grid.Lx <= p_x[i] && p_x[i] <= grid.Lx);
      assert(-grid.Ly <= p_y[i] && p_y[i] <= grid.Ly);

      ierr = PISMFEvoR::interp_field_point( p_x[i], p_y[i], p_z[i], pressure3, P  ); CHKERRQ(ierr);
      ierr = PISMFEvoR::interp_field_point( p_x[i], p_y[i], p_z[i], tauxz3   , txz); CHKERRQ(ierr);
      ierr = PISMFEvoR::interp_field_point( p_x[i], p_y[i], p_z[i], tauyz3   , tyz); CHKERRQ(ierr);
      
      /*std::vector<double> stress = {   P,   0, txz,
       *                                 0,   P, tyz,
       *                               txz, tyz,   P}; 
       * Woops, no list initialization in c++98. 
       */
      std::vector<double> stress(9,0.0);
      stress[1] = stress[5] = stress[9] = P; // FIXME correct sign?
      stress[3] = stress[7] = txz;           // FIXME correct sign?
      stress[6] = stress[8] = tyz;           // FIXME correct sign?
        // don't strictly need P here as we only need the deviatoric stress.
      
      ierr = PISMFEvoR::interp_field_point( p_x[i], p_y[i], p_z[i], m_enthalpy, E  ); CHKERRQ(ierr);
      ierr = m_EC->getAbsTemp(E, P, T); CHKERRQ(ierr);
      
      d_i.stepInTime  (T, stress, m_t, m_dt, nMigRe[i] , nPoly[i] , bulkEdot    );
      d_iso.stepInTime(T, stress, m_t, m_dt, nMigRe_iso, nPoly_iso, bulkEdot_iso);
      
      p_e[i] = tensorMagnitude(bulkEdot)/tensorMagnitude(bulkEdot_iso);
        
      // some bounds for the enhancement factor
      if (p_e[i] < 1.0) {
          p_e[i] = 1.0;
          // Enhance, not diminish.
      } else if (p_e[i] > 10.0) {
          p_e[i] = 10.0;
          // upper bound.
      }
    }
    
    // set the enhancement factor for every grid point from our particle cloud
    ierr PISMFEvoR::interp_grid_point(n_particles, p_x, p_z, p_e); CHKERRQ(ierr);
    
  }

  delete pressure;
  delete tauxz;
  delete tauyz;

  return 0;
}

PetscErrorCode PISMFEvoR::interp_field_point( double &x, double &y, double &z, 
                                                      IceModelVec3 *field3, 
                                                      double &feildValue) {
    PetscErrorCode ierr;
    
    int I = 0, J = 0;
    grid.compute_point_neighbors(x, y, I, J);
    std::vector<double> weights = grid.compute_interp_weights(x, y);

    double *column0 = NULL, *column1 = NULL, *column2 = NULL, *column3 = NULL;
    ierr = field3->getInternalColumn(I,   J,   &column0); CHKERRQ(ierr);
    ierr = field3->getInternalColumn(I+1, J,   &column1); CHKERRQ(ierr);
    ierr = field3->getInternalColumn(I+1, J+1, &column2); CHKERRQ(ierr);
    ierr = field3->getInternalColumn(I,   J+1, &column3); CHKERRQ(ierr);

    unsigned int K = 0;
    // K + 1 (used below) should be at most Mz - 1
    while (K + 1 < grid.Mz - 1 && grid.zlevels[K + 1] < z) {
    K++;
    }

    double z_weight = (z - grid.zlevels[K]) / (grid.zlevels[K+1] - grid.zlevels[K]);

    double f0 = column0[K] + z_weight * (column0[K+1] - column0[K]);
    double f1 = column1[K] + z_weight * (column1[K+1] - column1[K]);
    double f2 = column2[K] + z_weight * (column2[K+1] - column2[K]);
    double f3 = column3[K] + z_weight * (column3[K+1] - column3[K]);

    feildValue = weights[0] * f0 + weights[1] * f1 + weights[2] * f2 + weights[3] * f3;
    
    return 0;
}

PetscErrorCode PISMFEvoR::interp_grid_point(const unsigned int &n_particles, 
                                            const std::vector<double> &x, 
                                            const std::vector<double> &z, 
                                            const std::vector<double> &e) {
  Delaunay_triangulation D_TRI;
  
  // map our points to our function values
  std::map<Point, Field_type, Map_compare> function_values; 
  // function to access our data
  typedef CGAL::Data_access< std::map<Point, Field_type, Map_compare > > Value_access;
    
  // get the points for our convex hull
  for (unsigned int pn = 0, pn < n_particles; ++pn) {
    Point p(x[pn],z[pn]);
    D_TRI.insert( p );
    function_values.insert( std::make_pair(p, e[pn]) );
  }
        
  for (int i=grid.xs; i<grid.xs+grid.xm; ++i) {    
    for (unsigned int k=0; k<grid.Mz; ++k) {
      Point INTERP( grid.x(i),grid.zlevels(k) );
      
      // make a vector of the iterpolation point and type
      std::vector< std::pair< Point, Field_type > > coord;
      Field_type norm = CGAL::natural_neighbor_coordinates_2 (D_TRI, INTERP, std::back_inserter(coord) ).second;
      Field_type res =  CGAL::linear_interpolation (coord.begin(), coord.end(), norm, Value_access(function_values));
      
      // set m_enhancement_factor for all y grid points at INTERP(x,z)
      for (int j=grid.ys; j<grid.ys+grid.my; ++j) {
          m_enhancement_factor(i,j,k) = double(res);
      }
    }
  }
    
  return 0;
}

void PISMFEvoR::add_vars_to_output(const std::string &keyword, std::set<std::string> &result) {
  if (keyword != "none") {
    result.insert(m_enhancement_factor.metadata().get_string("short_name"));
  }
}

PetscErrorCode PISMFEvoR::define_variables(const std::set<std::string> &vars, const PIO &nc,
                                           IO_Type nctype) {
  PetscErrorCode ierr;
  if (set_contains(vars, "enhancement_factor")) {
    ierr = m_enhancement_factor.define(nc, nctype); CHKERRQ(ierr);
  }

  return 0;
}

PetscErrorCode PISMFEvoR::write_variables(const std::set<std::string> &vars, const PIO& nc) {
  PetscErrorCode ierr;
  if (set_contains(vars, "enhancement_factor")) {
    ierr = m_enhancement_factor.write(nc); CHKERRQ(ierr);
  }

  return 0;
}

PetscErrorCode PISMFEvoR::allocate() {
  PetscErrorCode ierr;

  // SIAFD diffusive flux computation requires stencil width of 1
  const unsigned int stencil_width = 1;

  ierr = m_enhancement_factor.create(grid, "enhancement_factor", WITH_GHOSTS,
                                     stencil_width); CHKERRQ(ierr);
  ierr = m_enhancement_factor.set_attrs("diagnostic", // i.e. not needed to re-start the model
                                        "flow law enhancement factor",
                                        "1", // dimensionless
                                        "" /* no standard name */); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode PISMFEvoR::init(Vars &vars) {
  PetscErrorCode ierr;
  ierr = verbPrintf(2, grid.com,
                    "* Initializing the Fabric Evolution with Recrystallization"
                    " (FEvoR) model...\n"); CHKERRQ(ierr);

  // make enhancement factor available to other PISM components:
  ierr = vars.add(m_enhancement_factor); CHKERRQ(ierr); //IceModelVec

  m_enthalpy = dynamic_cast<IceModelVec3*>(vars.get("enthalpy"));
  if (m_enthalpy == NULL) {
    SETERRQ(grid.com, 1, "enthalpy field is not available");
  }

  // It would be nice to be able to allocate these in
  // PISMFEvoR::allocate() or in the constructor, but pism::Vars is
  // not available there...
  if (m_pressure == NULL) {
    m_pressure = new PSB_pressure(m_stress_balance, grid, vars);
    assert(m_pressure != NULL);
  }

  if (m_tauxz == NULL) {
    m_tauxz = new PSB_tauxz(m_stress_balance, grid, vars);
    assert(m_tauxz != NULL);
  }

  if (m_tauyz == NULL) {
    m_tauyz = new PSB_tauyz(m_stress_balance, grid, vars);
    assert(m_tauyz != NULL);
  }

  return 0;
}

} // end of namespace pism