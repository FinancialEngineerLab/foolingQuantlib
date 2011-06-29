/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2007, 2009 StatPro Italia srl
 Copyright (C) 2011 Ferdinando Ametrano

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

#include <ql/pricingengines/swap/discountingswapengine.hpp>
#include <ql/cashflows/cashflows.hpp>
#include <ql/utilities/dataformatters.hpp>

namespace QuantLib {

    DiscountingSwapEngine::DiscountingSwapEngine(
                            const Handle<YieldTermStructure>& discountCurve,
                            boost::optional<bool> includeSettlementDateFlows,
                            Date settlementDate,
                            Date npvDate)
    : discountCurve_(discountCurve),
      includeSettlementDateFlows_(includeSettlementDateFlows),
      settlementDate_(settlementDate), npvDate_(npvDate) {
        registerWith(discountCurve_);
    }

    void DiscountingSwapEngine::calculate() const {
        QL_REQUIRE(!discountCurve_.empty(),
                   "discounting term structure handle is empty");

        results_.value = 0.0;
        results_.errorEstimate = Null<Real>();

        Date refDate = discountCurve_->referenceDate();

        Date settlementDate = settlementDate_;
        if (settlementDate_==Date()) {
            settlementDate = refDate;
        } else {
            QL_REQUIRE(settlementDate>=refDate,
                       "settlement date (" << settlementDate << ") before "
                       "discount curve reference date (" << refDate << ")");
        }

        results_.valuationDate = npvDate_;
        if (npvDate_==Date()) {
            results_.valuationDate = refDate;
        } else {
            QL_REQUIRE(npvDate_>=refDate,
                       "npv date (" << npvDate_  << ") before "
                       "discount curve reference date (" << refDate << ")");
        }
        results_.npvDateDiscount = discountCurve_->discount(results_.valuationDate);

        Size n = arguments_.legs.size();
        results_.legNPV.resize(n);
        results_.legBPS.resize(n);
        results_.startDiscounts.resize(n);

        bool includeRefDateFlows =
            includeSettlementDateFlows_ ?
            *includeSettlementDateFlows_ :
            Settings::instance().includeReferenceDateCashFlows();

        for (Size i=0; i<n; ++i) {
            try {
                const YieldTermStructure& discount_ref = **discountCurve_;
                CashFlows::npvbps(arguments_.legs[i],
                                  discount_ref,
                                  includeRefDateFlows,
                                  settlementDate,
                                  results_.valuationDate,
                                  results_.legNPV[i],
                                  results_.legBPS[i]);
                results_.legNPV[i] *= arguments_.payer[i];
                results_.legBPS[i] *= arguments_.payer[i];
            } catch (std::exception &e) {
                QL_FAIL(io::ordinal(i+1) << " leg: " << e.what());
            }
            results_.value += results_.legNPV[i];
            try {
                Date d = CashFlows::startDate(arguments_.legs[i]);
                results_.startDiscounts[i] = discountCurve_->discount(d);
            } catch (...) {
                results_.startDiscounts[i] = Null<DiscountFactor>();
            }
        }
    }

}