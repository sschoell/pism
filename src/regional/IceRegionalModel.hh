/* Copyright (C) 2015 PISM Authors
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

#ifndef _ICEREGIONALMODEL_H_
#define _ICEREGIONALMODEL_H_

#include "base/iceModel.hh"

namespace pism {

//! \brief A version of the PISM core class (IceModel) which knows about the
//! `no_model_mask` and its semantics.
class IceRegionalModel : public IceModel {
public:
  IceRegionalModel(IceGrid::Ptr g, Context::Ptr c);
protected:
  virtual void set_vars_from_options();
  virtual void bootstrap_2d(const std::string &filename);
  virtual void initFromFile(const std::string &filename);
  virtual void model_state_setup();
  virtual void createVecs();
  virtual void allocate_stressbalance();
  virtual void allocate_basal_yield_stress();
  virtual void massContExplicitStep();
  virtual void cell_interface_fluxes(bool dirichlet_bc,
                                     int i, int j,
                                     StarStencil<Vector2> input_velocity,
                                     StarStencil<double> input_flux,
                                     StarStencil<double> &output_velocity,
                                     StarStencil<double> &output_flux);
  virtual void enthalpyAndDrainageStep(unsigned int *vertSacrCount,
                                       double *liquifiedVol,
                                       unsigned int *bulgeCount);
private:
  IceModelVec2Int m_no_model_mask;
  IceModelVec2S   m_usurf_stored, m_thk_stored, m_bmr_stored;
};

} // end of namespace pism

#endif /* _ICEREGIONALMODEL_H_ */
