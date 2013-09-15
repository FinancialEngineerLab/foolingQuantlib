/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2013 Peter Caspers

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

/*! \file zabrsmilesection.hpp
    \brief zabr smile section
*/

#ifndef quantlib_zabr_smile_section_hpp
#define quantlib_zabr_smile_section_hpp

#include <ql/termstructures/volatility/smilesection.hpp>
#include <ql/time/daycounters/actual365fixed.hpp>
#include <ql/termstructures/volatility/zabr.hpp>
#include <vector>

namespace QuantLib {

    class ZabrSmileSection : public SmileSection {

      public:
        
        enum Evaluation { ShortMaturityLognormal = 0, ShortMaturityNormal = 1,
                          LocalVolatility = 2};

        ZabrSmileSection(Time timeToExpiry, Rate forward,
                         const std::vector<Real> &zabrParameters,
                         const Evaluation evaluation = ShortMaturityLognormal);
        ZabrSmileSection(const Date &d, Rate forward,
                         const std::vector<Real> &zabrParameters,
                         const DayCounter &dc = Actual365Fixed(),
                         const Evaluation evaluation = ShortMaturityLognormal);
        Real minStrike() const { return 0.0; } // revisit later ...
        Real maxStrike() const { return QL_MAX_REAL; }
        Real atmLevel() const { return model_->forward(); }
        Real optionPrice(Rate strike, Option::Type type = Option::Call,
                         Real discount = 1.0) const;

        boost::shared_ptr<ZabrModel> model() { return model_; }

      protected:
        Volatility volatilityImpl(Rate strike) const;

      private:
        void init();
        boost::shared_ptr<ZabrModel> model_;
        Evaluation evaluation_;
        Rate forward_;
        std::vector<Real> params_;

    };

}

#endif