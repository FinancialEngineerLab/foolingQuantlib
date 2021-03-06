/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2006, 2008 Ferdinando Ametrano
 Copyright (C) 2006 Fran?ois du Vignaud
 Copyright (C) 2006 Katiuscia Manzoni
 Copyright (C) 2000, 2001, 2002, 2003 RiskMap srl

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

#include <ql/termstructures/volatility/swaption/swaptionvolmatrix.hpp>
#include <ql/termstructures/volatility/flatsmilesection.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/utilities/dataformatters.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/math/interpolations/bilinearinterpolation.hpp>

namespace QuantLib {

    // floating reference date, floating market data
    SwaptionVolatilityMatrix::SwaptionVolatilityMatrix(
                    const Calendar& cal,
                    BusinessDayConvention bdc,
                    const std::vector<Period>& optionT,
                    const std::vector<Period>& swapT,
                    const std::vector<std::vector<Handle<Quote> > >& vols,
                    const DayCounter& dc)
    : SwaptionVolatilityDiscrete(optionT, swapT, 0, cal, bdc, dc),
      volHandles_(vols),
      volatilities_(vols.size(), vols.front().size()) {
        checkInputs(volatilities_.rows(), volatilities_.columns());
        registerWithMarketData();
        interpolation_ =
            BilinearInterpolation(swapLengths_.begin(), swapLengths_.end(),
                                  optionTimes_.begin(), optionTimes_.end(),
                                  volatilities_);
   }

    // fixed reference date, floating market data
    SwaptionVolatilityMatrix::SwaptionVolatilityMatrix(
                    const Date& refDate,
                    const Calendar& cal,
                    BusinessDayConvention bdc,
                    const std::vector<Period>& optionT,
                    const std::vector<Period>& swapT,
                    const std::vector<std::vector<Handle<Quote> > >& vols,
                    const DayCounter& dc)
    : SwaptionVolatilityDiscrete(optionT, swapT, refDate, cal, bdc, dc),
      volHandles_(vols),
      volatilities_(vols.size(), vols.front().size()) {
        checkInputs(volatilities_.rows(), volatilities_.columns());
        registerWithMarketData();
        interpolation_ =
            BilinearInterpolation(swapLengths_.begin(), swapLengths_.end(),
                                  optionTimes_.begin(), optionTimes_.end(),
                                  volatilities_);
    }

    // floating reference date, fixed market data
    SwaptionVolatilityMatrix::SwaptionVolatilityMatrix(
                        const Calendar& cal,
                        BusinessDayConvention bdc,
                        const std::vector<Period>& optionT,
                        const std::vector<Period>& swapT,
                        const Matrix& vols,
                        const DayCounter& dc)
    : SwaptionVolatilityDiscrete(optionT, swapT, 0, cal, bdc, dc),
      volHandles_(vols.rows()),
      volatilities_(vols.rows(), vols.columns()) {

        checkInputs(vols.rows(), vols.columns());

        // fill dummy handles to allow generic handle-based
        // computations later on
        for (Size i=0; i<vols.rows(); ++i) {
            volHandles_[i].resize(vols.columns());
            for (Size j=0; j<vols.columns(); ++j)
                volHandles_[i][j] = Handle<Quote>(boost::shared_ptr<Quote>(new
                    SimpleQuote(vols[i][j])));
        }
        interpolation_ =
            BilinearInterpolation(swapLengths_.begin(), swapLengths_.end(),
                                  optionTimes_.begin(), optionTimes_.end(),
                                  volatilities_);
    }

    // fixed reference date, fixed market data
    SwaptionVolatilityMatrix::SwaptionVolatilityMatrix(
                        const Date& refDate,
                        const Calendar& cal,
                        BusinessDayConvention bdc,
                        const std::vector<Period>& optionT,
                        const std::vector<Period>& swapT,
                        const Matrix& vols,
                        const DayCounter& dc)
    : SwaptionVolatilityDiscrete(optionT, swapT, refDate, cal, bdc, dc),
      volHandles_(vols.rows()),
      volatilities_(vols.rows(), vols.columns()) {

        checkInputs(vols.rows(), vols.columns());

        // fill dummy handles to allow generic handle-based
        // computations later on
        for (Size i=0; i<vols.rows(); ++i) {
            volHandles_[i].resize(vols.columns());
            for (Size j=0; j<vols.columns(); ++j)
                volHandles_[i][j] = Handle<Quote>(boost::shared_ptr<Quote>(new
                    SimpleQuote(vols[i][j])));
        }
        interpolation_ =
            BilinearInterpolation(swapLengths_.begin(), swapLengths_.end(),
                                  optionTimes_.begin(), optionTimes_.end(),
                                  volatilities_);
    }

    // fixed reference date and fixed market data, option dates
    SwaptionVolatilityMatrix::SwaptionVolatilityMatrix(
                    const Date& today,
                    const std::vector<Date>& optionDates,
                    const std::vector<Period>& swapT,
                    const Matrix& vols,
                    const DayCounter& dc)
    : SwaptionVolatilityDiscrete(optionDates, swapT, today, Calendar(), Following, dc),
      volHandles_(vols.rows()),
      volatilities_(vols.rows(), vols.columns()) {

        checkInputs(vols.rows(), vols.columns());

        // fill dummy handles to allow generic handle-based
        // computations later on
        for (Size i=0; i<vols.rows(); ++i) {
            volHandles_[i].resize(vols.columns());
            for (Size j=0; j<vols.columns(); ++j)
                volHandles_[i][j] = Handle<Quote>(boost::shared_ptr<Quote>(new
                    SimpleQuote(vols[i][j])));
        }
        interpolation_ =
            BilinearInterpolation(swapLengths_.begin(), swapLengths_.end(),
                                  optionTimes_.begin(), optionTimes_.end(),
                                  volatilities_);
    }

    void SwaptionVolatilityMatrix::checkInputs(Size volRows,
                                               Size volsColumns) const {
        QL_REQUIRE(nOptionTenors_==volRows,
                   "mismatch between number of option dates (" <<
                   nOptionTenors_ << ") and number of rows (" << volRows <<
                   ") in the vol matrix");
        QL_REQUIRE(nSwapTenors_==volsColumns,
                   "mismatch between number of swap tenors (" <<
                   nSwapTenors_ << ") and number of rows (" << volsColumns <<
                   ") in the vol matrix");
    }

    void SwaptionVolatilityMatrix::registerWithMarketData()
    {
        for (Size i=0; i<volHandles_.size(); ++i)
            for (Size j=0; j<volHandles_.front().size(); ++j)
                registerWith(volHandles_[i][j]);
    }

    void SwaptionVolatilityMatrix::performCalculations() const {

        SwaptionVolatilityDiscrete::performCalculations();

        // we might use iterators here...
        for (Size i=0; i<volatilities_.rows(); ++i)
            for (Size j=0; j<volatilities_.columns(); ++j)
                volatilities_[i][j] = volHandles_[i][j]->value();
    }

    //boost::shared_ptr<SmileSection>
    //SwaptionVolatilityMatrix::smileSectionImpl(const Date& d,
    //                                           const Period& swapTenor) const {
    //    Time optionTime = timeFromReference(d);
    //    Time swapLength = convertSwapTenor(swapTenor);
    //    // dummy strike
    //    Volatility atmVol = volatilityImpl(optionTime, swapLength, 0.05);
    //    return boost::shared_ptr<SmileSection>(new
    //        FlatSmileSection(d, atmVol, dayCounter(), referenceDate()));
    //}

    boost::shared_ptr<SmileSection>
    SwaptionVolatilityMatrix::smileSectionImpl(Time optionTime,
                                               Time swapLength) const {
        // dummy strike
        Volatility atmVol = volatilityImpl(optionTime, swapLength, 0.05);
        return boost::shared_ptr<SmileSection>(new
            FlatSmileSection(optionTime, atmVol, dayCounter()));
    }

}
