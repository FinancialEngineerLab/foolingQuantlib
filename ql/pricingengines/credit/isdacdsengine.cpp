/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2014 Jose Aparicio
 Copyright (C) 2014 Peter Caspers

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

#include <ql/pricingengines/credit/isdacdsengine.hpp>
#include <ql/instruments/claim.hpp>
#include <ql/cashflows/fixedratecoupon.hpp>
#include <ql/termstructures/yield/piecewiseyieldcurve.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/termstructures/credit/piecewisedefaultcurve.hpp>
#include <ql/termstructures/credit/flathazardrate.hpp>
#include <ql/math/interpolations/forwardflatinterpolation.hpp>
#include <ql/time/calendars/weekendsonly.hpp>
#include <ql/time/daycounters/actual360.hpp>

#include <boost/make_shared.hpp>

#include <iostream>

namespace QuantLib {

    IsdaCdsEngine::IsdaCdsEngine(
        const Handle<DefaultProbabilityTermStructure> &probability,
        Real recoveryRate, const Handle<YieldTermStructure> &discountCurve,
        boost::optional<bool> includeSettlementDateFlows,
        const NumericalFix numericalFix, const AccrualBias accrualBias,
        const ForwardsInCouponPeriod forwardsInCouponPeriod)
        : probability_(probability), recoveryRate_(recoveryRate),
          discountCurve_(discountCurve),
          includeSettlementDateFlows_(includeSettlementDateFlows),
          numericalFix_(numericalFix), accrualBias_(accrualBias),
          forwardsInCouponPeriod_(forwardsInCouponPeriod) {

        registerWith(probability_);
        registerWith(discountCurve_);
    }

    IsdaCdsEngine::IsdaCdsEngine(
        const std::vector<boost::shared_ptr<DefaultProbabilityHelper>> &
            probabilityHelpers,
        Real recoveryRate,
        const std::vector<boost::shared_ptr<RateHelper>> &rateHelpers,
        boost::optional<bool> includeSettlementDateFlows,
        const NumericalFix numericalFix, const AccrualBias accrualBias,
        const ForwardsInCouponPeriod forwardsInCouponPeriod)
        : probabilityHelpers_(probabilityHelpers), recoveryRate_(recoveryRate),
          rateHelpers_(rateHelpers),
          includeSettlementDateFlows_(includeSettlementDateFlows),
          numericalFix_(numericalFix), accrualBias_(accrualBias),
          forwardsInCouponPeriod_(forwardsInCouponPeriod) {

        discountCurve_ = Handle<YieldTermStructure>(
            boost::make_shared<PiecewiseYieldCurve<Discount, LogLinear>>(
                0, WeekendsOnly(), rateHelpers_, Actual365Fixed()));

        discountCurve_->enableExtrapolation();

        for (Size i = 0; i < probabilityHelpers_.size(); i++) {
            boost::shared_ptr<CdsHelper> h =
                boost::dynamic_pointer_cast<CdsHelper>(probabilityHelpers_[i]);
            QL_REQUIRE(h != NULL, "Cds helper required");
            h->setDiscountCurve(discountCurve_);
        }

        probability_ =
            Handle<DefaultProbabilityTermStructure>(boost::make_shared<
                PiecewiseDefaultCurve<SurvivalProbability, LogLinear>>(
                0, WeekendsOnly(), probabilityHelpers_, Actual365Fixed()));

        registerWith(probability_);
        registerWith(discountCurve_);
    }

    void IsdaCdsEngine::calculate() const {

        QL_REQUIRE(numericalFix_ == None || numericalFix_ == Taylor,
                   "numerical fix must be None or Taylor");
        QL_REQUIRE(accrualBias_ == HalfDayBias || accrualBias_ == NoBias,
                   "accrual bias must be HalfDayBias or NoBias");
        QL_REQUIRE(forwardsInCouponPeriod_ == Flat ||
                       forwardsInCouponPeriod_ == Piecewise,
                   "forwards in coupon period must be Flat or Piecewise");

        // it would be possible to handle the cases which are excluded below,
        // but the ISDA engine is not explicitly specified to handle them,
        // so we just forbid them too

        Actual365Fixed dc;
        Actual360 dc1;
        Actual360 dc2(true);

        Date evalDate = Settings::instance().evaluationDate();

        // check if given curves are ISDA compatible
        // (the interpolation is checked below)

        QL_REQUIRE(!discountCurve_.empty(), "no discount term structure set");
        QL_REQUIRE(!probability_.empty(), "no probability term structure set");
        QL_REQUIRE(discountCurve_->dayCounter() == dc,
                   "yield term structure day counter ("
                       << discountCurve_->dayCounter()
                       << ") should be Act/365(Fixed)");
        QL_REQUIRE(probability_->dayCounter() == dc,
                   "probability term structure day counter ("
                       << probability_->dayCounter() << ") should be "
                       << "Act/365(Fixed)");
        QL_REQUIRE(discountCurve_->referenceDate() == evalDate,
                   "yield term structure reference date ("
                       << discountCurve_->referenceDate()
                       << " should be evaluation date (" << evalDate << ")");
        QL_REQUIRE(probability_->referenceDate() == evalDate,
                   "probability term structure reference date ("
                       << probability_->referenceDate()
                       << " should be evaluation date (" << evalDate << ")");
        QL_REQUIRE(arguments_.settlesAccrual,
                   "ISDA engine not compatible with non accrual paying CDS");
        QL_REQUIRE(arguments_.paysAtDefaultTime,
                   "ISDA engine not compatible with end period payment");
        QL_REQUIRE(boost::dynamic_pointer_cast<FaceValueClaim>(
                       arguments_.claim) != NULL,
                   "ISDA engine not compatible with non face value claim");

        Date maturity = arguments_.maturity;
        Date effectiveProtectionStart =
            std::max<Date>(arguments_.protectionStart, evalDate + 1);

        // collect nodes from both curves and sort them

        boost::shared_ptr<PiecewiseYieldCurve<Discount, LogLinear>> castY1 =
            boost::dynamic_pointer_cast<
                PiecewiseYieldCurve<Discount, LogLinear>>(*discountCurve_);

        boost::shared_ptr<PiecewiseYieldCurve<ForwardRate, BackwardFlat>>
        castY2 = boost::dynamic_pointer_cast<
            PiecewiseYieldCurve<ForwardRate, BackwardFlat>>(*discountCurve_);

        boost::shared_ptr<FlatForward> castY3 =
            boost::dynamic_pointer_cast<FlatForward>(*discountCurve_);

        boost::shared_ptr<PiecewiseDefaultCurve<SurvivalProbability, LogLinear>>
        castC1 = boost::dynamic_pointer_cast<
            PiecewiseDefaultCurve<SurvivalProbability, LogLinear>>(
            *probability_);

        boost::shared_ptr<PiecewiseDefaultCurve<HazardRate, BackwardFlat>>
        castC2 = boost::dynamic_pointer_cast<
            PiecewiseDefaultCurve<HazardRate, BackwardFlat>>(*probability_);

        boost::shared_ptr<FlatHazardRate> castC3 =
            boost::dynamic_pointer_cast<FlatHazardRate>(*probability_);

        QL_REQUIRE(castY1 != NULL || castY2 != NULL || castY3 != NULL,
                   "Yield curve must be flat forward interpolated");
        QL_REQUIRE(castC1 != NULL || castC2 != NULL || castC3 != NULL,
                   "Credit curve must be flat forward interpolated");

        std::vector<Date> yDates, cDates;

        if (castY1 != NULL)
            yDates = castY1->dates();
        if (castY2 != NULL)
            yDates = castY2->dates();
        if (castC1 != NULL)
            cDates = castC1->dates();
        if (castC2 != NULL)
            cDates = castC2->dates();

        std::vector<Date> nodesTmp(yDates);
        nodesTmp.insert(nodesTmp.end(), cDates.begin(), cDates.end());
        std::sort(nodesTmp.begin(), nodesTmp.end());

        // add protection start date and cut all nodes earlier than this date
        // add maturity date and cut all nodes later than this date

        std::vector<Date>::iterator it0 = std::upper_bound(
            nodesTmp.begin(), nodesTmp.end(), effectiveProtectionStart - 1);
        nodesTmp.insert(it0, effectiveProtectionStart);
        std::vector<Date>::iterator it1 =
            std::upper_bound(nodesTmp.begin(), nodesTmp.end(), maturity);
        nodesTmp.insert(it1, maturity);

        it0 = std::upper_bound(nodesTmp.begin(), nodesTmp.end(),
                               effectiveProtectionStart - 1);
        it1 = std::upper_bound(nodesTmp.begin(), nodesTmp.end(), maturity);
        std::vector<Date>::iterator it = std::unique(it0, it1);

        std::vector<Date> nodes(it0, it);

        //debug
        std::cout << "***debug engine - nodes ...:" << std::endl;
        for(Size i=0;i<nodes.size();i++) {
            std::cout << nodes[i] << std::endl;
        }

        const Real nFix = (numericalFix_ == None ? 1E-50 : 0.0);

        // protection leg pricing (npv is always negative at this stage)

        Real protectionNpv = 0.0;

        Date d0 = effectiveProtectionStart;
        // Size i =
        //     (std::upper_bound(nodes.begin(), nodes.end(), d0) - nodes.begin()) -
        //     1;
        Size i=0;
        Date d1 = d0;

        int e = 1;
        do {
            ++i;
            d1 = std::min<Date>(nodes[i], maturity);
            if(d1==maturity)
                e=0;
            // Real P0 = discountCurve_->discount(nodes[i]);
            // Real P1 = discountCurve_->discount(nodes[i + 1]);
            // Real Q0 = probability_->survivalProbability(nodes[i]);
            // Real Q1 = probability_->survivalProbability(nodes[i + 1]);
            Real P0 = discountCurve_->discount(d0);
            Real P1 = discountCurve_->discount(d1);
            Real Q0 = probability_->survivalProbability(d0-1); // these are_ end_ of day probs
            Real Q1 = probability_->survivalProbability(d1-e);
            std::cout << "period " << d0 << " to " << d1 << " q0=" << Q0 << " q1=" << Q1 << std::endl;

            Real fhat = std::log(P0) - std::log(P1);
            Real hhat = std::log(Q0) - std::log(Q1);
            Real fhphh = fhat + hhat;
            // std::cout << " fhat=" << fhat << " hhat=" << hhat;

            if (fhphh < 1E-4 && numericalFix_ == Taylor) {
                std::cout << "...applying numerical fix...." << std::endl;
                Real fhphhq = fhphh * fhphh;
                // terms up to (f+h)^3 seem more than enough,
                // what exactly is implemented in the standard isda C code ?
                protectionNpv +=
                    P0 * Q0 * hhat * (1.0 - 0.5 * fhphh + 1.0 / 6.0 * fhphhq -
                                      1.0 / 24.0 * fhphhq * fhphh);
            } else {
                protectionNpv += hhat / (fhphh + nFix) * (P0 * Q0 - P1 * Q1);
            }
            // std::cout << " cumProtNpv = " << protectionNpv << std::endl;
            d0 = d1;
        } while(d1<maturity);

        protectionNpv *= arguments_.claim->amount(
            Null<Date>(), arguments_.notional, recoveryRate_);

        results_.defaultLegNPV = protectionNpv;

        std::cout << "default leg npv = " << std::setprecision(12) << std::fixed << protectionNpv << std::endl;

        // premium leg pricing (npv is always positive at this stage)

        Real premiumNpv = 0.0, defaultAccrualNpv = 0.0;
        for (Size i = 0; i < arguments_.leg.size(); ++i) {
            boost::shared_ptr<FixedRateCoupon> coupon =
                boost::dynamic_pointer_cast<FixedRateCoupon>(arguments_.leg[i]);

            QL_REQUIRE(coupon->dayCounter() == dc ||
                           coupon->dayCounter() == dc1 ||
                           coupon->dayCounter() == dc2,
                       "ISDA engine requires a coupon day counter Act/365Fixed "
                           << "or Act/360 (" << coupon->dayCounter() << ")");

            // premium coupons

            if (!arguments_.leg[i]->hasOccurred(evalDate,
                                                includeSettlementDateFlows_)) {
                premiumNpv +=
                    coupon->amount() *
                    discountCurve_->discount(coupon->date()) *
                    probability_->survivalProbability(coupon->accrualEndDate());
                // std::cout << "Coupon #" << i << " has amount " <<
                // coupon->amount() << std::endl;
            }

            // default accruals

            if (!detail::simple_event(coupon->accrualEndDate())
                     .hasOccurred(effectiveProtectionStart, false)) {
                Date start = std::max<Date>(coupon->accrualStartDate(),
                                            effectiveProtectionStart);
                Date end = coupon->accrualEndDate();
                Real tstart =
                    discountCurve_->timeFromReference(start) -
                    (accrualBias_ == HalfDayBias ? -1.0 / 730.0 : 0.0);
                Real tend = discountCurve_->timeFromReference(end);
                std::vector<Date> localNodes;
                if (forwardsInCouponPeriod_ == Piecewise) {
                    std::vector<Date>::const_iterator it0 =
                        std::upper_bound(nodes.begin(), nodes.end(), start - 1);
                    std::vector<Date>::const_iterator it1 =
                        std::upper_bound(nodes.begin(), nodes.end(), end);
                    localNodes = std::vector<Date>(it0, it1);
                    if (localNodes.size() == 0) {
                        // both iterators point to the same element
                        localNodes.push_back(start);
                        localNodes.push_back(end);
                    }
                    if (start != *localNodes.begin())
                        localNodes.insert(localNodes.begin(), start);
                    if (end != localNodes.back())
                        localNodes.insert(localNodes.end(), end);
                } else { // setting flat => ignore intermediate curve nodes
                    localNodes.push_back(start);
                    localNodes.push_back(end);
                }

                Real defaultAccrThisNode = 0.;
                int e=1;
                for (Size k = 0; k < localNodes.size() - 1; k++) {
                    if(localNodes[k+1]==maturity)
                        e=0;
                    Real t0 = discountCurve_->timeFromReference(localNodes[k]);
                    Real t1 =
                        discountCurve_->timeFromReference(localNodes[k + 1]);
                    Real P0 = discountCurve_->discount(localNodes[k]);
                    Real Q0 =
                        probability_->survivalProbability(localNodes[k]-1);
                    Real P1 = discountCurve_->discount(localNodes[k + 1]);
                    Real Q1 =
                        probability_->survivalProbability(localNodes[k + 1]-e);
                    Real fhat = std::log(P0) - std::log(P1);
                    Real hhat = std::log(Q0) - std::log(Q1);
                    Real fhphh = fhat + hhat;
                    if (fhphh < 1E-4 && numericalFix_ == Taylor) {
                        // see above, terms up to (f+h)^3 seem more than enough,
                        // what exactly is implemented in the standard isda C
                        // code ?
                        Real fhphhq = fhphh * fhphh;
                        defaultAccrThisNode +=
                            hhat * P0 * Q0 *
                            ((t0 - tstart) *
                                 (1.0 - 0.5 * fhphh + 1.0 / 6.0 * fhphhq -
                                  1.0 / 24.0 * fhphhq * fhphh) +
                             (t1 - t0) *
                                 (0.5 - 1.0 / 3.0 * fhphh + 1.0 / 8.0 * fhphhq -
                                  1.0 / 30.0 * fhphhq * fhphh));
                    } else {
                        defaultAccrThisNode +=
                            (hhat / (fhphh + nFix)) *
                            ((t1 - t0) * ((P0 * Q0 - P1 * Q1) / (fhphh + nFix) -
                                          P1 * Q1) +
                             (t0 - tstart) * (P0 * Q0 - P1 * Q1));
                    }
                }
                Real eta = coupon->accrualPeriod() / (tend - tstart);
                defaultAccrualNpv +=
                    defaultAccrThisNode * coupon->amount() * eta;
            }
        }

        std::cout << "premiumNPV=" << premiumNpv << " defaultAccruals=" <<
         defaultAccrualNpv << std::endl;

        results_.couponLegNPV = premiumNpv + defaultAccrualNpv;

        // upfront flow npv

        std::cout << "check1" << std::endl;

        Real upfPVO1 = 0.0;
        results_.upfrontNPV = 0.0;
        if (arguments_.upfrontPayment &&
            !arguments_.upfrontPayment->hasOccurred(
                evalDate, includeSettlementDateFlows_)) {
            upfPVO1 =
                discountCurve_->discount(arguments_.upfrontPayment->date());
            results_.upfrontNPV = upfPVO1 * arguments_.upfrontPayment->amount();
        }
        std::cout << "check2" << std::endl;

        results_.accrualRebateNPV = 0.;
        if (arguments_.accrualRebate &&
            !arguments_.accrualRebate->hasOccurred(
                evalDate, includeSettlementDateFlows_)) {
            results_.accrualRebateNPV =
                discountCurve_->discount(arguments_.accrualRebate->date()) *
                arguments_.accrualRebate->amount();
        }

        Real upfrontSign = Protection::Seller ? 1.0 : -1.0;

        if (arguments_.side == Protection::Seller) {
            results_.defaultLegNPV *= -1.0;
            results_.accrualRebateNPV *= -1.0;
        } else {
            results_.couponLegNPV *= -1.0;
            results_.upfrontNPV *= -1.0;
        }
        std::cout << "check3" << std::endl;

        results_.value = results_.defaultLegNPV + results_.couponLegNPV +
                         results_.upfrontNPV + results_.accrualRebateNPV;

        // std::cout << "upfrontNPV=" << results_.upfrontNPV << " (amount=" << arguments_.upfrontPayment->amount() << ")" << std::endl;
        std::cout << "accrualRebateNPV=" << results_.accrualRebateNPV << std::endl;
        std::cout << " (amount=" << arguments_.accrualRebate->amount() << ")" << std::endl;

        results_.errorEstimate = Null<Real>();

        if (results_.couponLegNPV != 0.0) {
            results_.fairSpread =
                -results_.defaultLegNPV * arguments_.spread /
                (results_.couponLegNPV + results_.accrualRebateNPV);
        } else {
            results_.fairSpread = Null<Rate>();
        }

        Real upfrontSensitivity = upfPVO1 * arguments_.notional;
        if (upfrontSensitivity != 0.0) {
            results_.fairUpfront =
                -upfrontSign * (results_.defaultLegNPV + results_.couponLegNPV +
                                results_.accrualRebateNPV) /
                upfrontSensitivity;
        } else {
            results_.fairUpfront = Null<Rate>();
        }

        static const Rate basisPoint = 1.0e-4;

        if (arguments_.spread != 0.0) {
            results_.couponLegBPS =
                results_.couponLegNPV * basisPoint / arguments_.spread;
        } else {
            results_.couponLegBPS = Null<Rate>();
        }

        if (arguments_.upfront && *arguments_.upfront != 0.0) {
            results_.upfrontBPS =
                results_.upfrontNPV * basisPoint / (*arguments_.upfront);
        } else {
            results_.upfrontBPS = Null<Rate>();
        }
    }
}
