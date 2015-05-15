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

/*! \file mcfxtarfengine.hpp
    \brief Monte Carlo engine for FX Tarf
*/

#ifndef quantlib_pricingengines_mc_fxtarf_hpp
#define quantlib_pricingengines_mc_fxtarf_hpp

#include <ql/experimental/fx/fxtarfengine.hpp>
#include <ql/event.hpp>
#include <ql/pricingengines/mcsimulation.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/math/generallinearleastsquares.hpp>

#include <boost/make_shared.hpp>

#include <iostream> // only necessary for logging

namespace QuantLib {

namespace {

// raw data for proxy estimation
// the data vector is organized as follows
// level 0 = openFixings (e.g. 6 5 4 3 2 1 = Indices 5 ... 0)
// level 1 = accumulated amount buckets
//    [ previously accumuated amount = a(0), a(1) ]
//    [ a(1), a(2) ]
//    ...
//    [ a(n-1), a(n) = target ]
// level 2 = vector of pairs (spot, npv), sorted asc by spot values

typedef std::vector<std::vector<std::vector<std::pair<Real, Real> > > >
    ProxyData;

// regression function basis
Real basis0(const double) { return 1; }
Real basis1(const double x) { return x; }
Real basis2(const double x) { return x * x; }

} // empty namespace

template <class RNG = PseudoRandom, class S = Statistics>
class McFxTarfEngine : public FxTarfEngine,
                       public McSimulation<SingleVariate, RNG, S> {
  public:
    /*! proxy function giving a function spot => npv for one segment
        (bucket accumulated amount, number of open fixings)
        the function is given by two quadratic polynomials on intervals
        (-\infty,cutoff] and (cutoff,\infty).
        Only the ascending (long calls) or descending (long puts) branch
        is used and then extrapolated flat.
        For calls the extrapolation below the given lowerCutoff is done
        linear (for puts above this value).
    */
    class QuadraticProxyFunction : public FxTarf::Proxy::ProxyFunction {
      public:
        QuadraticProxyFunction(Option::Type type, const Real cutoff,
                               const Real a1, const Real b1, const Real c1,
                               const Real a2, const Real b2, const Real c2,
                               const Real lowerCutoff, const Real coreRegionMin,
                               const Real coreRegionMax);
        Real operator()(const Real spot) const;
        std::pair<Real, Real> coreRegion() const {
            return std::make_pair(coreRegionMin_, coreRegionMax_);
        }

      private:
        Option::Type type_;
        const Real a1_, b1_, c1_, a2_, b2_, c2_;
        const Real cutoff_, lowerCutoff_;
        const Real coreRegionMin_, coreRegionMax_;
        int flatExtrapolationType1_,
            flatExtrapolationType2_; // +1 = right, -1 = left
        Real extrapolationPoint1_, extrapolationPoint2_;
    };

    /*! typedefs */
    typedef typename McSimulation<SingleVariate, RNG, S>::path_generator_type
        path_generator_type;
    typedef typename McSimulation<SingleVariate, RNG, S>::path_pricer_type
        path_pricer_type;
    typedef typename McSimulation<SingleVariate, RNG, S>::stats_type stats_type;

    //! constructor
    McFxTarfEngine(
        const boost::shared_ptr<GeneralizedBlackScholesProcess> &process,
        Size timeSteps, Size timeStepsPerYears, bool brownianBridge,
        bool antitheticVariate, Size requiredSamples, Real requiredTolerance,
        Size maxSamples, BigNatural seed,
        const Handle<YieldTermStructure> discount, const bool generateProxy);

    void calculate() const;
    void reset();

  protected:
    // McSimulation Interface
    TimeGrid timeGrid() const;
    boost::shared_ptr<path_generator_type> pathGenerator() const;
    boost::shared_ptr<path_pricer_type> pathPricer() const;
    // data members
    boost::shared_ptr<GeneralizedBlackScholesProcess> process_;
    Size timeSteps_, timeStepsPerYear_;
    Size requiredSamples_, maxSamples_;
    Real requiredTolerance_;
    bool brownianBridge_;
    BigNatural seed_;
    bool generateProxy_;
    mutable std::vector<Real> fixingTimes_, discounts_;
    // proxy information generated by the engine
    mutable boost::shared_ptr<FxTarf::Proxy> proxy_;
    // simulation data on which the proxy is estimated
    mutable ProxyData data_;
    // bucket limits for collected data
    mutable std::vector<Real> accBucketLimits_;
};

//! Proxy function
template <class RNG, class S>
McFxTarfEngine<RNG, S>::QuadraticProxyFunction::QuadraticProxyFunction(
    Option::Type type, const Real cutoff, const Real a1, const Real b1,
    const Real c1, const Real a2, const Real b2, const Real c2,
    const Real lowerCutoff, const Real coreRegionMin, const Real coreRegionMax)
    : type_(type), cutoff_(cutoff), a1_(a1), b1_(b1), c1_(c1), a2_(a2), b2_(b2),
      c2_(c2), lowerCutoff_(lowerCutoff), coreRegionMin_(coreRegionMin),
      coreRegionMax_(coreRegionMax) {
    QL_REQUIRE((type == Option::Call && lowerCutoff_ <= cutoff) ||
                   (type == Option::Put && lowerCutoff_ >= cutoff),
               "lowerCutoff (" << lowerCutoff_
                               << ") must be less or equal (call) or greater "
                                  "equal (put) than cutoff ("
                               << cutoff
                               << ") for type "
                               << type_);
    // for calls we want ascending, for puts descending functions
    if (close(a1_, 0.0)) {
        QL_REQUIRE(b1_ > 0.0, "for a call and a1=0 b ("
                                  << b1_ << ") must be positive");
    } else {
        extrapolationPoint1_ = -b1_ / (2.0 * a1_);
        flatExtrapolationType1_ =
            (type_ == Option::Call ? 1.0 : -1.0) * (a1_ > 0.0 ? -1 : 1);
    }
    if (close(a2_, 0.0)) {
        QL_REQUIRE(b2_ > 0.0, "for a call and a2=0 b ("
                                  << b2_ << ") must be positive");
    } else {
        extrapolationPoint2_ = -b2_ / (2.0 * a2_);
        flatExtrapolationType2_ =
            (type_ == Option::Call ? 1.0 : -1.0) * (a2_ > 0.0 ? -1 : 1);
    }
}

template <class RNG, class S>
Real McFxTarfEngine<RNG, S>::QuadraticProxyFunction::
operator()(const Real spot) const {
    Real x = spot;
    if (spot <= cutoff_) {
        if (spot <= lowerCutoff_ && type_ == Option::Call &&
            flatExtrapolationType1_ == 1) {
            // linear extrapolation instead of quadratic outside lowerCutoff
            // if flat extrapolation is to the right)
            return (2.0 * a1_ * lowerCutoff_ + b1_) * spot + c1_ -
                   a1_ * lowerCutoff_ * lowerCutoff_;
        }
        x = flatExtrapolationType1_ *
            std::min(flatExtrapolationType1_ * extrapolationPoint1_,
                     flatExtrapolationType1_ * x);
        Real tmp = a1_ * x * x + b1_ * x + c1_;
        // ensure global monotonicity
        if (type_ == Option::Put) {
            Real ct = flatExtrapolationType2_ *
                      std::min(flatExtrapolationType2_ * extrapolationPoint2_,
                               flatExtrapolationType2_ * cutoff_);
            tmp = std::max(a2_ * ct * ct + b2_ * ct + c2_, tmp);
        }
        return tmp;
    } else {
        if (spot >= lowerCutoff_ && type_ == Option::Put &&
            flatExtrapolationType2_ == -1) {
            // linear extrapolation instead of quadratic outside lowerCutoff if
            // flat extrapolation is to the left
            return (2.0 * a2_ * lowerCutoff_ + b2_) * spot + c2_ -
                   a2_ * lowerCutoff_ * lowerCutoff_;
        }
        x = flatExtrapolationType2_ *
            std::min(flatExtrapolationType2_ * extrapolationPoint2_,
                     flatExtrapolationType2_ * x);
        Real tmp = a2_ * x * x + b2_ * x + c2_;
        // ensure global monotonicity
        if (type_ == Option::Call) {
            Real ct = flatExtrapolationType1_ *
                      std::min(flatExtrapolationType1_ * extrapolationPoint1_,
                               flatExtrapolationType1_ * cutoff_);
            tmp = std::max(a1_ * ct * ct + b1_ * ct + c1_, tmp);
        }
        return tmp;
    }
}

//! Monte Carlo fx-tarf engine factory
template <class RNG = PseudoRandom, class S = Statistics>
class MakeMcFxTarfEngine {
  public:
    MakeMcFxTarfEngine(
        const boost::shared_ptr<GeneralizedBlackScholesProcess> &);
    // named parameters
    MakeMcFxTarfEngine &withSteps(Size steps);
    MakeMcFxTarfEngine &withStepsPerYear(Size steps);
    MakeMcFxTarfEngine &withBrownianBridge(bool b = true);
    MakeMcFxTarfEngine &withAntitheticVariate(bool b = true);
    MakeMcFxTarfEngine &withSamples(Size samples);
    MakeMcFxTarfEngine &withAbsoluteTolerance(Real tolerance);
    MakeMcFxTarfEngine &withMaxSamples(Size samples);
    MakeMcFxTarfEngine &withSeed(BigNatural seed);
    MakeMcFxTarfEngine &
    withDiscount(const Handle<YieldTermStructure> &discount);
    MakeMcFxTarfEngine &withProxy(bool b = true);
    // conversion to pricing engine
    operator boost::shared_ptr<PricingEngine>() const;

  private:
    boost::shared_ptr<GeneralizedBlackScholesProcess> process_;
    bool brownianBridge_, antithetic_;
    Size steps_, stepsPerYear_, samples_, maxSamples_;
    Real tolerance_;
    BigNatural seed_;
    Handle<YieldTermStructure> discount_;
    bool generateProxy_;
};

//! Path Pricer
class FxTarfPathPricer : public PathPricer<Path> {
  public:
    FxTarfPathPricer(const std::vector<Real> &fixingTimes,
                     const std::vector<Real> &discounts,
                     const Real accumulatedAmount, const Real sourceNominal,
                     const Real target, const FxTarf *instrument,
                     ProxyData &data, const std::vector<Real> &accBucketLimits,
                     const Date lastPaymentDate,
                     const Handle<YieldTermStructure> &discount,
                     const bool generateProxy);
    Real operator()(const Path &path) const;

  private:
    const std::vector<Real> &fixingTimes_, &discounts_;
    const Real accumulatedAmount_, sourceNominal_, target_;
    const FxTarf *instrument_;
    mutable std::vector<Size> fixingIndices_;
    ProxyData &data_;
    const std::vector<Real> &accBucketLimits_;
    const Date lastPaymentDate_;
    const Handle<YieldTermStructure> discount_;
    const bool generateProxy_;
};

// Implementation

template <class RNG, class S>
McFxTarfEngine<RNG, S>::McFxTarfEngine(
    const boost::shared_ptr<GeneralizedBlackScholesProcess> &process,
    Size timeSteps, Size timeStepsPerYear, bool brownianBridge,
    bool antitheticVariate, Size requiredSamples, Real requiredTolerance,
    Size maxSamples, BigNatural seed, const Handle<YieldTermStructure> discount,
    const bool generateProxy)
    : FxTarfEngine(discount),
      McSimulation<SingleVariate, RNG, S>(antitheticVariate, false),
      process_(process), timeSteps_(timeSteps),
      timeStepsPerYear_(timeStepsPerYear), requiredSamples_(requiredSamples),
      maxSamples_(maxSamples), requiredTolerance_(requiredTolerance),
      brownianBridge_(brownianBridge), seed_(seed),
      generateProxy_(generateProxy) {
    QL_REQUIRE(timeSteps != Null<Size>() || timeStepsPerYear != Null<Size>(),
               "no time steps provided");
    QL_REQUIRE(timeSteps == Null<Size>() || timeStepsPerYear == Null<Size>(),
               "both time steps and time steps per year were provided");
    QL_REQUIRE(timeSteps != 0, "timeSteps must be positive, "
                                   << timeSteps << " not allowed");
    QL_REQUIRE(timeStepsPerYear != 0, "timeStepsPerYear must be positive, "
                                          << timeStepsPerYear
                                          << " not allowed");
    QL_REQUIRE(!discount_.empty(), "no discount curve given");
    registerWith(process_);
}

template <class RNG, class S> void McFxTarfEngine<RNG, S>::reset() {
    FxTarfEngine::reset();
    fixingTimes_.clear();
    discounts_.clear();
    proxy_ = boost::shared_ptr<FxTarf::Proxy>();
    data_.clear();
    accBucketLimits_.clear();
}

template <class RNG, class S> void McFxTarfEngine<RNG, S>::calculate() const {

    Date today = Settings::instance().evaluationDate();

    // handle the trivial cases
    FxTarfEngine::calculate();

    // are we already done, i.e. has the base engine set the npv ?
    if (results_.value != Null<Real>())
        return;

    // we have at least one fixing left which is tommorow or later
    for (Size i = 0; i < arguments_.openFixingDates.size(); ++i) {
        fixingTimes_.push_back(process_->time(arguments_.openFixingDates[i]));
        discounts_.push_back(
            discount_->discount(arguments_.openPaymentDates[i]));
    }

    // prepare the data container on which the proxy pricing is estimated
    // later

    // we use a number of heuristics in the following
    // number of buckets for accumulated amounts
    // which are merged though if the data in the buckets
    // does not meet the requirement below
    Size nAccBuckets = 5;
    // first the data points per accumulated amount bucket should
    // be more than the total number of data points divided by dFactor
    Size dFactor = 10;
    // then (spot,npv) pairs are divided into two segments (for calls)
    // [spotMin,spotMin+relCutoff*(spotMax-spotMin)) and
    // [spotMin+relCutoff*(spotMax-spotMin,spotMax]
    // for puts we use 1.0-relCutoff to divide the intervall instead
    Real relCutoff = 0.80;
    // we require minCutoffRatio*(1.0-relCutoff)*totalNoDataPoints
    // to be still in the smaller (spot,npv) segment, otherwise
    // the cutoff will be lowered (calls) by a factor of
    // cutoffShrinkFactor until we reach this critical size
    Real minCutoffRatio = 0.33;
    Real cutoffShrinkFactor = 0.99;
    // on the lower bound (for calls) a lowerCutoff is determined
    // such that more than 1-minLowerExtr points are above this
    // cutoff. Below this threshhold, a linear extrapolation is
    // used.
    Real minLowerExtr = 0.05;
    // if the intersection of the two quadratic functions lies
    // within (1-smoothInt)*cutoff, (1+smoothInt)*cutoff
    // the cutoff is moved to the intersection points
    // Real smoothInt = 0.05;
    // the "trusted" region (aka core region) is defined by chopping
    // off the lower and upper coreCutoff part of the data
    Real coreCutoff = 0.01;

    // this is the minimum number of points required for regression
    Size minRegPoints = 3;

    // set the number format for logging output
    // std::cerr << std::setprecision(12) << std::fixed;

    if (generateProxy_) {
        // create the buckets
        // std::cerr << "Buckets for accumulated amounts" << std::endl;
        for (Size i = 0; i < nAccBuckets; ++i) {
            accBucketLimits_.push_back(
                static_cast<Real>(i) / static_cast<Real>(nAccBuckets) *
                    (arguments_.target - arguments_.accumulatedAmount) +
                arguments_.accumulatedAmount);
            // std::cerr << "bucket;" << i << ";" << accBucketLimits_.back()
            // << std::endl;
        }
        // std::cerr << std::endl;

        // we set the first bucket limit to zero, which does not change
        // anything, but leaves no room that the given accumulated amount
        // is below the first limit
        accBucketLimits_[0] = 0.0;

        // initialize the data container
        for (Size i1 = 0; i1 < fixingTimes_.size(); ++i1) {
            std::vector<std::vector<std::pair<Real, Real> > > level1Tmp;
            for (Size i2 = 0; i2 < nAccBuckets; ++i2) {
                level1Tmp.push_back(std::vector<std::pair<Real, Real> >(0));
            }
            data_.push_back(level1Tmp);
        }
    }

    // do the main calculation using the mc machinery
    McSimulation<SingleVariate, RNG, S>::calculate(
        requiredTolerance_, requiredSamples_, maxSamples_);
    results_.value =
        this->mcModel_->sampleAccumulator().mean() + unsettledAmountNpv_;
    if (RNG::allowsErrorEstimate)
        results_.errorEstimate =
            this->mcModel_->sampleAccumulator().errorEstimate();

    if (!generateProxy_)
        return;

    // create the proxy object and initialize the members
    proxy_ = boost::make_shared<FxTarf::Proxy>();
    proxy_->origEvalDate = today;
    proxy_->openFixingDates = arguments_.openFixingDates;
    proxy_->accBucketLimits = accBucketLimits_;
    proxy_->lastPaymentDate = arguments_.schedule.dates().back();
    for (Size i = 0; i < proxy_->openFixingDates.size(); ++i) {
        std::vector<boost::shared_ptr<FxTarf::Proxy::ProxyFunction> > fctVecTmp(
            accBucketLimits_.size());
        proxy_->functions.push_back(fctVecTmp);
    }

    // do the regression on appropriately merged data sets
    for (Size i = 0; i < arguments_.openFixingDates.size(); ++i) {
        // std::cerr << "open Fixings index i=" << i << " (i.e. " << (i + 1)
        //           << " open fixings left)" << std::endl;

        // get the data for the specific number of open fixing times
        std::vector<std::vector<std::pair<Real, Real> > > &tmp = data_[i];

        // how many data points do we have over all
        // accumulated amount buckets ?
        Size numberOfDataPoints = 0;
        for (Size j = 0; j < tmp.size(); ++j)
            numberOfDataPoints += tmp[j].size();

        // merge data if pieces are too small
        // std::cerr << "total number of data points = " <<
        // numberOfDataPoints
        //           << std::endl;

        Size k0 = 0, k0Before = 0;
        do {
            std::vector<std::pair<Real, Real> > xTmp;
            Real spotMin = QL_MAX_REAL, spotMax = QL_MIN_REAL;
            do {
                std::vector<std::pair<Real, Real> > xTmp2(xTmp.size() +
                                                          tmp[k0].size());
                std::merge(xTmp.begin(), xTmp.end(), tmp[k0].begin(),
                           tmp[k0].end(), xTmp2.begin());
                xTmp.swap(xTmp2);
                if (tmp[k0].size() > 0) {
                    if (tmp[k0].front().first < spotMin)
                        spotMin = tmp[k0].front().first;
                    if (tmp[k0].back().first > spotMax)
                        spotMax = tmp[k0].back().first;
                }
                // std::cerr
                //     << "collected accumulated amount segment with index
                //     k="
                //     << k0 << " yielding a total size of " << xTmp.size()
                //     << std::endl;
                k0++;
            } while (dFactor * xTmp.size() < numberOfDataPoints);

            // count the number of remaining data points ...
            Size remainingDataPoints = 0;
            for (Size j = k0; j < tmp.size(); ++j)
                remainingDataPoints += tmp[j].size();

            // std::cerr << "remaining data points = " <<
            // remainingDataPoints
            //           << std::endl;

            // ... and join the rest of data if they are to few
            if (dFactor * remainingDataPoints < numberOfDataPoints) {
                for (Size j = k0; j < tmp.size(); ++j) {
                    std::vector<std::pair<Real, Real> > xTmp2(xTmp.size() +
                                                              tmp[j].size());
                    std::merge(xTmp.begin(), xTmp.end(), tmp[j].begin(),
                               tmp[j].end(), xTmp2.begin());
                    xTmp.swap(xTmp2);
                    if (tmp[j].size() > 0) {
                        if (tmp[j].front().first < spotMin)
                            spotMin = tmp[j].front().first;
                        if (tmp[j].back().first > spotMax)
                            spotMax = tmp[j].back().first;
                    }
                }
                k0 = tmp.size();
                // std::cerr << "merged also the rest of the data because"
                //           << "it contains too few points, new total size
                //           is "
                //           << xTmp.size() << std::endl;
            }
            // std::cerr << "data set statistics: min spot " << spotMin
            //           << " max spot " << spotMax << std::endl;

            // we rearrange the data to get two segments for the spot
            bool isCall = arguments_.longPositionType == Option::Call;
            Real relCutoffTmp = isCall ? relCutoff : 1.0 - relCutoff;
            std::vector<Real> xTmp1, xTmp2, yTmp1, yTmp2;
            Real cutoff = spotMin + relCutoffTmp * (spotMax - spotMin);

            // we want a certain percentage of data still in the smaller
            // data set, otherwise we lower the cutoff
            Size minDataSegment =
                static_cast<Size>((1.0 - relCutoffTmp) * minCutoffRatio *
                                  xTmp.size()) +
                1;
            Size sizeA, sizeB, criticalSize;
            do {
                sizeA = std::upper_bound(xTmp.begin(), xTmp.end(),
                                         std::make_pair(cutoff, 0.0)) -
                        xTmp.begin();
                sizeB = xTmp.size() - sizeA;
                // std::cerr << std::setprecision(12)
                //           << "cutoff factor=" << relCutoffTmp
                //           << ", cutoff=" << cutoff
                //           << ", segments size = " << sizeA << "," <<
                //           sizeB
                //           << " minimum size required " << minDataSegment
                //           << std::endl;
                criticalSize = isCall ? sizeB : sizeA;
                if (((isCall && relCutoffTmp > 0.5) ||
                     (!isCall && relCutoffTmp < 0.5)) &&
                    (criticalSize < minDataSegment ||
                     criticalSize < minRegPoints)) {
                    if (isCall)
                        relCutoffTmp *= cutoffShrinkFactor;
                    else
                        relCutoffTmp /= std::min(cutoffShrinkFactor, 1.0);
                    cutoff = spotMin + relCutoffTmp * (spotMax - spotMin);
                    // std::cerr << "too few data in critical segment "
                    //           << " (" << criticalSize
                    //           << "), adjust cutoff factor to " <<
                    //           relCutoffTmp
                    //           << " and cutoff to " << cutoff <<
                    //           std::endl;
                }
            } while (
                ((isCall && relCutoffTmp > 0.5) ||
                 (!isCall && relCutoffTmp < 0.5)) &&
                (criticalSize < minDataSegment || criticalSize < minRegPoints));

            // copy the data to the final vectors used for the regression
            for (Size i = 0; i < xTmp.size(); ++i) {
                if (xTmp[i].first <= cutoff) {
                    xTmp1.push_back(xTmp[i].first);
                    yTmp1.push_back(xTmp[i].second);
                } else {
                    xTmp2.push_back(xTmp[i].first);
                    yTmp2.push_back(xTmp[i].second);
                }
            }

            // determine lower cutoff (in terms of calls), below which we
            // extrapolate linear
            Real lowerCutoff =
                xTmp[xTmp.size() * (isCall ? minLowerExtr : 1.0 - minLowerExtr)]
                    .first;
            // determine the core (trusted) region
            Real coreRegionMin =
                xTmp[static_cast<int>(xTmp.size() * coreCutoff)].first;
            Real coreRegionMax =
                xTmp[static_cast<int>(xTmp.size() * (1.0 - coreCutoff))].first;
            // std::cerr << "Lower cutoff point set to " << lowerCutoff
            //           << std::endl;

            // the function object
            boost::shared_ptr<FxTarf::Proxy::ProxyFunction> fct;

            // if minSpot = cutoff = maxSpot (this may happen at t = 0) we
            // just return a constant function being the average over all
            // data points
            if (std::fabs(spotMax - spotMin) < QL_EPSILON) {
                Real avg = 0.0;
                for (Size i = 0; i < xTmp1.size(); ++i)
                    avg += yTmp1[i];
                for (Size i = 0; i < xTmp2.size(); ++i)
                    avg += yTmp2[i];
                avg /= static_cast<double>(xTmp1.size() + xTmp2.size());
                fct = boost::make_shared<QuadraticProxyFunction>(
                    arguments_.longPositionType, cutoff, 0.0, 0.0, avg, 0.0,
                    0.0, avg, -QL_MAX_REAL, spotMin, spotMax);
                // std::cerr
                //     << "regression produced a constant function with
                //     value "
                //     << avg << std::endl;
            } else {

                // final sanity check before regression
                QL_REQUIRE(xTmp1.size() >= 3,
                           "too few points for regression in set 1 ("
                               << xTmp1.size() << ")");
                QL_REQUIRE(xTmp2.size() >= 3,
                           "too few points for regression in set 2 ("
                               << xTmp2.size() << ")");

                // regression
                std::vector<boost::function<Real(Real)> > v;
                v.push_back(&basis0);
                v.push_back(&basis1);
                v.push_back(&basis2);

                GeneralLinearLeastSquares ls1(xTmp1, yTmp1, v);
                Array result1 = ls1.coefficients();

                // std::cerr << "regression results (set1 size " <<
                // xTmp1.size()
                //           << " set2 size " << xTmp2.size() << ")" <<
                //           std::endl;
                // std::cerr << "f1(x)=" << result1[0] << "+x*" <<
                // result1[1]
                //           << "+x**2*" << result1[2] << std::endl;

                GeneralLinearLeastSquares ls2(xTmp2, yTmp2, v);
                Array result2 = ls2.coefficients();

                // std::cerr << "f2(x)=" << result2[0] << "+x*" <<
                // result2[1]
                //           << "+x**2*" << result2[2] << std::endl;

                // check if the cutoff should be moved to the intersection
                // point of the two overlapping functions
                Real a1 = result1[2];
                Real b1 = result1[1];
                Real c1 = result1[0];
                Real a2 = result2[2];
                Real b2 = result2[1];
                Real c2 = result2[0];
                // std::cerr << "cutoff before adjustment = " << cutoff << " ";
                // if (close(a1, a2)) {
                //     if (!close(b1, b2)) {
                //         Real tmp = -(c1 - c2) / (b1 - b2);
                //         if (fabs((tmp - cutoff) / cutoff) < smoothInt) {
                //             cutoff = tmp;
                //         }
                //     }
                // } else {
                //     Real tmp1 =
                //         (-(b1 - b2) + std::sqrt((b1 - b2) * (b1 - b2) -
                //                                 4.0 * (a1 - a2) * (c1 - c2)))
                //                                 /
                //         (2.0 * (a1 - a2));
                //     Real tmp2 =
                //         (-(b1 - b2) - std::sqrt((b1 - b2) * (b1 - b2) -
                //                                 4.0 * (a1 - a2) * (c1 - c2)))
                //                                 /
                //         (2.0 * (a1 - a2));
                //     // std::cerr << " tmp1=" << tmp1 << " tmp2=" << tmp2 << "
                //     ";
                //     if (fabs((tmp1 - cutoff) / cutoff) < smoothInt)
                //         cutoff = tmp1;
                //     if (fabs((tmp2 - cutoff) / cutoff) < smoothInt)
                //         cutoff = tmp2;
                // }
                // std::cerr << "cutoff after adjustment = " << cutoff <<
                // std::endl;

                // make sure that lower cutoff is still left from cutoff
                lowerCutoff = std::min(lowerCutoff, cutoff);

                fct = boost::make_shared<QuadraticProxyFunction>(
                    arguments_.longPositionType, cutoff, a1, b1, c1, a2, b2, c2,
                    lowerCutoff, coreRegionMin, coreRegionMax);
            }

            // store the proxy function, please note that
            // due to merging we may have the same function for several
            // accumulated amount segments
            for (Size kk = k0Before; kk < k0; ++kk) {
                proxy_->functions[i][kk] = fct;
                // std::cerr << "set computed function on indices "
                //              "(openFixings,accAmount) = (" << i << "," <<
                //              kk
                //           << ")" << std::endl;
            }
            k0Before = k0;
            // std::cerr << "done, proceed with next openFixing index
            // ...\n\n";
        } while (k0 < tmp.size()); // do-while over accumulated amount buckets
    }                              // for openFixingTimes

    // set proxy information generated during simulation as result

    results_.proxy = this->proxy_;
}

template <class RNG, class S>
TimeGrid McFxTarfEngine<RNG, S>::timeGrid() const {
    if (timeSteps_ != Null<Size>()) {
        return TimeGrid(fixingTimes_.begin(), fixingTimes_.end(), timeSteps_);
    } else if (timeStepsPerYear_ != Null<Size>()) {
        Size steps = static_cast<Size>(timeStepsPerYear_ * fixingTimes_.back());
        return TimeGrid(fixingTimes_.begin(), fixingTimes_.end(),
                        std::max<Size>(steps, 1));
    } else {
        QL_FAIL("time steps not specified");
    }
}

template <class RNG, class S>
boost::shared_ptr<typename McFxTarfEngine<RNG, S>::path_generator_type>
McFxTarfEngine<RNG, S>::pathGenerator() const {
    TimeGrid grid = timeGrid();
    typename RNG::rsg_type gen =
        RNG::make_sequence_generator(grid.size() - 1, seed_);
    return boost::make_shared<path_generator_type>(process_, grid, gen,
                                                   brownianBridge_);
}

template <class RNG, class S>
boost::shared_ptr<typename McFxTarfEngine<RNG, S>::path_pricer_type>
McFxTarfEngine<RNG, S>::pathPricer() const {
    return boost::make_shared<FxTarfPathPricer>(
        fixingTimes_, discounts_, arguments_.accumulatedAmount,
        arguments_.sourceNominal, arguments_.target, arguments_.instrument,
        this->data_, this->accBucketLimits_, arguments_.schedule.dates().back(),
        this->discount_, this->generateProxy_);
}

template <class RNG, class S>
inline MakeMcFxTarfEngine<RNG, S>::MakeMcFxTarfEngine(
    const boost::shared_ptr<GeneralizedBlackScholesProcess> &process)
    : process_(process), brownianBridge_(false), antithetic_(false),
      steps_(Null<Size>()), stepsPerYear_(Null<Size>()), samples_(Null<Size>()),
      maxSamples_(Null<Size>()), tolerance_(Null<Real>()), seed_(0),
      discount_(process->riskFreeRate()), generateProxy_(false) {}

template <class RNG, class S>
inline MakeMcFxTarfEngine<RNG, S> &
MakeMcFxTarfEngine<RNG, S>::withSteps(Size steps) {
    steps_ = steps;
    return *this;
}

template <class RNG, class S>
inline MakeMcFxTarfEngine<RNG, S> &
MakeMcFxTarfEngine<RNG, S>::withStepsPerYear(Size steps) {
    stepsPerYear_ = steps;
    return *this;
}

template <class RNG, class S>
inline MakeMcFxTarfEngine<RNG, S> &
MakeMcFxTarfEngine<RNG, S>::withBrownianBridge(bool brownianBridge) {
    brownianBridge_ = brownianBridge;
    return *this;
}

template <class RNG, class S>
inline MakeMcFxTarfEngine<RNG, S> &
MakeMcFxTarfEngine<RNG, S>::withAntitheticVariate(bool b) {
    antithetic_ = b;
    return *this;
}

template <class RNG, class S>
inline MakeMcFxTarfEngine<RNG, S> &
MakeMcFxTarfEngine<RNG, S>::withSamples(Size samples) {
    QL_REQUIRE(tolerance_ == Null<Real>(), "tolerance already set");
    samples_ = samples;
    return *this;
}

template <class RNG, class S>
inline MakeMcFxTarfEngine<RNG, S> &
MakeMcFxTarfEngine<RNG, S>::withAbsoluteTolerance(Real tolerance) {
    QL_REQUIRE(samples_ == Null<Size>(), "number of samples already set");
    QL_REQUIRE(RNG::allowsErrorEstimate, "chosen random generator policy "
                                         "does not allow an error estimate");
    tolerance_ = tolerance;
    return *this;
}

template <class RNG, class S>
inline MakeMcFxTarfEngine<RNG, S> &
MakeMcFxTarfEngine<RNG, S>::withMaxSamples(Size samples) {
    maxSamples_ = samples;
    return *this;
}

template <class RNG, class S>
inline MakeMcFxTarfEngine<RNG, S> &
MakeMcFxTarfEngine<RNG, S>::withSeed(BigNatural seed) {
    seed_ = seed;
    return *this;
}

template <class RNG, class S>
inline MakeMcFxTarfEngine<RNG, S> &MakeMcFxTarfEngine<RNG, S>::withDiscount(
    const Handle<YieldTermStructure> &discount) {
    discount_ = discount;
    return *this;
}

template <class RNG, class S>
inline MakeMcFxTarfEngine<RNG, S> &
MakeMcFxTarfEngine<RNG, S>::withProxy(bool b) {
    generateProxy_ = b;
    return *this;
}

template <class RNG, class S>
inline MakeMcFxTarfEngine<RNG, S>::
operator boost::shared_ptr<PricingEngine>() const {
    QL_REQUIRE(steps_ != Null<Size>() || stepsPerYear_ != Null<Size>(),
               "number of steps not given");
    QL_REQUIRE(steps_ == Null<Size>() || stepsPerYear_ == Null<Size>(),
               "number of steps overspecified");
    return boost::shared_ptr<PricingEngine>(new McFxTarfEngine<RNG, S>(
        process_, steps_, stepsPerYear_, brownianBridge_, antithetic_, samples_,
        tolerance_, maxSamples_, seed_, discount_, generateProxy_));
}

} // namespace QuantLib
#endif
