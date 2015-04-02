/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2007, 2008 Ferdinando Ametrano
 Copyright (C) 2007 Fran�ois du Vignaud
 Copyright (C) 2007 Katiuscia Manzoni
 Copyright (C) 2007 Giorgio Facchinetti

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

/*! \file optionletstripper1.hpp
    \brief optionlet (caplet/floorlet) volatility stripper
*/

#ifndef quantlib_optionletstripper1_hpp
#define quantlib_optionletstripper1_hpp

#include <ql/termstructures/volatility/optionlet/optionletstripper.hpp>

namespace QuantLib {

    class CapFloor;
    class SimpleQuote;

    typedef std::vector<std::vector<boost::shared_ptr<CapFloor> > > CapFloorMatrix;

    /*! Helper class to strip optionlet (i.e. caplet/floorlet) volatilities
        (a.k.a. forward-forward volatilities) from the (cap/floor) term
        volatilities of a CapFloorTermVolSurface.
    */
    class OptionletStripper1 : public OptionletStripper {
      public:
        OptionletStripper1(const boost::shared_ptr<CapFloorTermVolSurface> &,
                           const boost::shared_ptr<IborIndex> &index,
                           Rate switchStrikes = Null<Rate>(),
                           Real accuracy = 1.0e-6, Natural maxIter = 100,
                           const Handle<YieldTermStructure> &discount =
                               Handle<YieldTermStructure>(),
                           Model model = ShiftedLognormal,
                           Real displacement = 0.0);

        const Matrix& capFloorPrices() const;
        const Matrix& capFloorVolatilities() const;
        const Matrix& optionletPrices() const;
        Rate switchStrike() const;

        //! \name LazyObject interface
        //@{
        void performCalculations() const;
        //@}
      private:
        mutable Matrix capFloorPrices_, optionletPrices_;
        mutable Matrix capFloorVols_;
        mutable Matrix optionletStDevs_;

        mutable CapFloorMatrix capFloors_;
        mutable std::vector<std::vector<boost::shared_ptr<SimpleQuote> > > volQuotes_;
        bool floatingSwitchStrike_;
        mutable bool capFlooMatrixNotInitialized_;
        mutable Rate switchStrike_;
        Real accuracy_;
        Natural maxIter_;
    };

}

#endif
