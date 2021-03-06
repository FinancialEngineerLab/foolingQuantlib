/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2013 Jose Aparicio

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

#include <ql/experimental/credit/blackcmcdsengine.hpp>
#include <ql/instruments/claim.hpp>
#include <ql/termstructures/defaulttermstructure.hpp>
#include <ql/termstructures/yieldtermstructure.hpp>
#include <ql/experimental/credit/creditcouponpricer.hpp>
#include <ql/experimental/credit/creditcmcoupon.hpp>
#include <ql/experimental/credit/defaultevent.hpp>
#include <ql/experimental/credit/creditindex.hpp>

namespace QuantLib {

    BalckConstantMaturityCDSEngine::BalckConstantMaturityCDSEngine(
            const Handle<DefaultProbabilityTermStructure>& dfts,
            Real recoveryRate,
            const Handle<YieldTermStructure>& discountCurve,
            const Handle<Quote>& vol,
            boost::optional<bool> includeSettlementDateFlows)
    : probability_(dfts), recoveryRate_(recoveryRate), 
      discountCurve_(discountCurve), vol_(vol), 
      includeSettlementDateFlows_(includeSettlementDateFlows) 
    {
        registerWith(vol_);
        registerWith(probability_);
        registerWith(discountCurve_);
    }

    void BalckConstantMaturityCDSEngine::calculate() const {
        QL_REQUIRE(!discountCurve_.empty(),
                   "no discount term structure set");
        QL_REQUIRE(!probability_.empty(),
                   "no probability term structure set");

        Date today = Settings::instance().evaluationDate();
        Date settlementDate = discountCurve_->referenceDate();

        // recreate local, it is dependent:
        CdsCmCouponPricer couponPricer(vol_->value(), recoveryRate_);

        results_.couponLegNPV = 0.;
        results_.defaultLegNPV = 0.;

        // check for defaults and give a jump to default price if any
        boost::shared_ptr<DefaultEvent> defEvent = 
            arguments_.creditIndex->issuer().defaultedBetween(
                arguments_.protectionStart, today, 
                arguments_.creditIndex->defaultKey(), true);
        if(defEvent) {
            if(!defEvent->settlement().hasOccurred()
                ||
                !defEvent->hasSettled()) {
                /* Arguably, since theres is no settlement date. Means the jump 
                   to default value is set to the one with the largest default 
                   leg possible:
                */
                results_.defaultLegNPV = arguments_.claim->amount(
                    probability_->calendar().advance(today, 1, Days),
                    arguments_.notional,
                    recoveryRate_
                    );
            }
            // current coupon in default:
            for (Size i=0; i<arguments_.leg.size(); ++i) {
                if (arguments_.leg[i]->hasOccurred(settlementDate,
                                                   includeSettlementDateFlows_))
                    break;
                boost::shared_ptr<CmCdsCoupon> currentCoupon = 
                    boost::dynamic_pointer_cast<CmCdsCoupon>(arguments_.leg[i]);

				// duplicated code; same loop as below with no default test
				couponPricer.initialize(*currentCoupon);
                if(currentCoupon->isCapped()) {
                    results_.couponLegNPV += 
                        couponPricer.capletPrice(currentCoupon->cap());
                }else{
                    results_.couponLegNPV += couponPricer.swapletPrice();
                }
                //do not treat further coupons, only the (current) defaulted one
                break;
            }
            // stop treatment, leave results with no fair gearing
            results_.couponLegNPV *= arguments_.notional;
            results_.value = results_.couponLegNPV - results_.defaultLegNPV;
            if(arguments_.side == Protection::Buyer) {
                results_.value *= -1.;
                results_.couponLegNPV *= -1.;
            }
            return;
        }

        /* no defaults, npv: (this section duplicates the coupon leg code but 
		it avoids going twice through the loop or writting spaghetti logic) */
        for (Size i=0; i<arguments_.leg.size(); ++i) {
            if (arguments_.leg[i]->hasOccurred(settlementDate,
                                               includeSettlementDateFlows_))
                break;
            boost::shared_ptr<CmCdsCoupon> coupon = 
                boost::dynamic_pointer_cast<CmCdsCoupon>(arguments_.leg[i]);

            couponPricer.initialize(*coupon);
            if(coupon->isCapped()) {
                results_.couponLegNPV += 
                    couponPricer.capletPrice(coupon->cap());
            }else{
                results_.couponLegNPV += couponPricer.swapletPrice();
            }

            // Default leg computed on each period:
            Date paymentDate = coupon->date(),
                 startDate = coupon->accrualStartDate(),
                 endDate = coupon->accrualEndDate();
            // this is the only point where it might not coincide
            if (i==0)
                startDate = arguments_.protectionStart;
            Date effectiveStartDate =
                (startDate <= today && today <= endDate) ? today : startDate;
            Date defaultDate = // mid-point
                effectiveStartDate + (endDate-effectiveStartDate)/2;

            Probability P = probability_->defaultProbability(
                                                effectiveStartDate,
                                                endDate);

            // on the other side, we add the payment in case of default.
            Real claim = arguments_.claim->amount(defaultDate,
                                                  arguments_.notional,
                                                  recoveryRate_);
            if (arguments_.paysAtDefaultTime) {
                results_.defaultLegNPV +=
                    P * claim * discountCurve_->discount(defaultDate);
            } else {
                results_.defaultLegNPV +=
                    P * claim * discountCurve_->discount(paymentDate);
            }
        }

        results_.couponLegNPV *= arguments_.notional;

        // done, update engine results
        results_.value = results_.couponLegNPV - results_.defaultLegNPV;
        if(arguments_.side == Protection::Buyer) {
            results_.value *= -1.;
            results_.couponLegNPV *= -1.;
        }

        results_.fairGearingFactor =  std::abs(results_.defaultLegNPV * 
            arguments_.gearing / results_.couponLegNPV);
        
        }

}
