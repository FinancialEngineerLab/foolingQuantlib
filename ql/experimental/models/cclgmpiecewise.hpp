/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2015 Peter Caspers

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

/*! \file cclgmpiecewise.hpp
    \brief cross currency parametrization using piecewise alpha, constant kappa,
           piecewise fx sigma, constant correlation
*/

#ifndef quantlib_cclgm_piecewise_hpp
#define quantlib_cclgm_piecewise_hpp

namespace QuantLib {

class CcLgmPiecewise : CcLgmParametrization<CcLgmPiecewise> {
public:
    typedef LgmFxPiecewiseSigma fx_parametrization;
    typedef LgmPiecewiseAlphaConstantKappa lgm_parametrization;

    

private:

}

} // namespace QuantLib

#endif