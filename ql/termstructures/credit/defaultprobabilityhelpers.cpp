/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2008, 2009 Jose Aparicio
 Copyright (C) 2008 Chris Kenyon
 Copyright (C) 2008 Roland Lichters
 Copyright (C) 2008 StatPro Italia srl

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

#include <ql/termstructures/credit/defaultprobabilityhelpers.hpp>
#include <ql/instruments/creditdefaultswap.hpp>
#include <ql/pricingengines/credit/midpointcdsengine.hpp>
#include <ql/pricingengines/credit/isdacdsengine.hpp>

#include <boost/make_shared.hpp>

#include <iostream>

namespace QuantLib {

    namespace {
        void no_deletion(DefaultProbabilityTermStructure*) {}
    }

    CdsHelper::CdsHelper(const Handle<Quote> &quote, const Period &tenor,
                         Integer settlementDays, const Calendar &calendar,
                         Frequency frequency,
                         BusinessDayConvention paymentConvention,
                         DateGeneration::Rule rule,
                         const DayCounter &dayCounter, Real recoveryRate,
                         const Handle<YieldTermStructure> &discountCurve,
                         bool settlesAccrual, bool paysAtDefaultTime,
                         const DayCounter &lastPeriodDayCounter,
                         const bool rebatesAccrual, const bool useIsdaEngine)
        : RelativeDateDefaultProbabilityHelper(quote), tenor_(tenor),
          settlementDays_(settlementDays), calendar_(calendar),
          frequency_(frequency), paymentConvention_(paymentConvention),
          rule_(rule), dayCounter_(dayCounter), recoveryRate_(recoveryRate),
          discountCurve_(discountCurve), settlesAccrual_(settlesAccrual),
          paysAtDefaultTime_(paysAtDefaultTime),
          lastPeriodDC_(lastPeriodDayCounter), rebatesAccrual_(rebatesAccrual),
          useIsdaEngine_(useIsdaEngine), isdaNumericalFix_(IsdaCdsEngine::Taylor),
          isdaAccrualBias_(IsdaCdsEngine::NoBias),
          isdaForwardsInCouponPeriod_(IsdaCdsEngine::Piecewise) {

        initializeDates();

        registerWith(discountCurve);
    }

    CdsHelper::CdsHelper(Rate quote, const Period &tenor,
                         Integer settlementDays, const Calendar &calendar,
                         Frequency frequency,
                         BusinessDayConvention paymentConvention,
                         DateGeneration::Rule rule,
                         const DayCounter &dayCounter, Real recoveryRate,
                         const Handle<YieldTermStructure> &discountCurve,
                         bool settlesAccrual, bool paysAtDefaultTime,
                         const DayCounter &lastPeriodDayCounter,
                         const bool rebatesAccrual, const bool useIsdaEngine)
        : RelativeDateDefaultProbabilityHelper(quote), tenor_(tenor),
          settlementDays_(settlementDays), calendar_(calendar),
          frequency_(frequency), paymentConvention_(paymentConvention),
          rule_(rule), dayCounter_(dayCounter), recoveryRate_(recoveryRate),
          discountCurve_(discountCurve), settlesAccrual_(settlesAccrual),
          paysAtDefaultTime_(paysAtDefaultTime),
          lastPeriodDC_(lastPeriodDayCounter), rebatesAccrual_(rebatesAccrual),
          useIsdaEngine_(useIsdaEngine), isdaNumericalFix_(IsdaCdsEngine::Taylor),
          isdaAccrualBias_(IsdaCdsEngine::NoBias),
          isdaForwardsInCouponPeriod_(IsdaCdsEngine::Piecewise) {

        initializeDates();

        registerWith(discountCurve);
    }

    void CdsHelper::setTermStructure(DefaultProbabilityTermStructure* ts) {
        RelativeDateDefaultProbabilityHelper::setTermStructure(ts);

        probability_.linkTo(
            boost::shared_ptr<DefaultProbabilityTermStructure>(ts, no_deletion),
            false);

        resetEngine();
    }

    void CdsHelper::update() {
        RelativeDateDefaultProbabilityHelper::update();
        resetEngine();
    }

    void CdsHelper::initializeDates() {
        protectionStart_ = evaluationDate_ + settlementDays_;
        Date startDate = calendar_.adjust(protectionStart_,
                                          paymentConvention_);
        //Date endDate = evaluationDate_ + tenor_; // see below

        if (rule_ == DateGeneration::CDS) { // for standard CDS ..
            // .. the start date is not adjusted
            startDate = protectionStart_;
        }
        // .. and (in any case) the end date rolls by 3 month as
        //  soon as the trade date falls on an IMM date
        Date endDate = protectionStart_ + tenor_;

        schedule_ =
            MakeSchedule().from(startDate)
                          .to(endDate)
                          .withFrequency(frequency_)
                          .withCalendar(calendar_)
                          .withConvention(paymentConvention_)
                          .withTerminationDateConvention(Unadjusted)
                          .withRule(rule_);
        earliestDate_ = schedule_.dates().front();
        latestDate_   = calendar_.adjust(schedule_.dates().back(),
                                         paymentConvention_);
        if(useIsdaEngine_) ++latestDate_;
    }

    SpreadCdsHelper::SpreadCdsHelper(
                              const Handle<Quote>& runningSpread,
                              const Period& tenor,
                              Integer settlementDays,
                              const Calendar& calendar,
                              Frequency frequency,
                              BusinessDayConvention paymentConvention,
                              DateGeneration::Rule rule,
                              const DayCounter& dayCounter,
                              Real recoveryRate,
                              const Handle<YieldTermStructure>& discountCurve,
                              bool settlesAccrual,
                              bool paysAtDefaultTime,
                              const DayCounter& lastPeriodDayCounter,
                              const bool rebatesAccrual,
                              const bool useIsdaEngine)
    : CdsHelper(runningSpread, tenor, settlementDays, calendar,
                frequency, paymentConvention, rule, dayCounter,
                recoveryRate, discountCurve, settlesAccrual,
                paysAtDefaultTime,lastPeriodDayCounter,rebatesAccrual,
                useIsdaEngine) {}

    SpreadCdsHelper::SpreadCdsHelper(
                              Rate runningSpread,
                              const Period& tenor,
                              Integer settlementDays,
                              const Calendar& calendar,
                              Frequency frequency,
                              BusinessDayConvention paymentConvention,
                              DateGeneration::Rule rule,
                              const DayCounter& dayCounter,
                              Real recoveryRate,
                              const Handle<YieldTermStructure>& discountCurve,
                              bool settlesAccrual,
                              bool paysAtDefaultTime,
                              const DayCounter& lastPeriodDayCounter,
                              const bool rebatesAccrual,
                              const bool useIsdaEngine)
    : CdsHelper(runningSpread, tenor, settlementDays, calendar,
                frequency, paymentConvention, rule, dayCounter,
                recoveryRate, discountCurve, settlesAccrual,
                paysAtDefaultTime,lastPeriodDayCounter,rebatesAccrual,
                useIsdaEngine) {}

    Real SpreadCdsHelper::impliedQuote() const {
        swap_->recalculate();
        return swap_->fairSpread();
    }

    void SpreadCdsHelper::initializeDates() {
        CdsHelper::initializeDates();
    }

    void SpreadCdsHelper::resetEngine() {
        swap_ = boost::shared_ptr<CreditDefaultSwap>(new CreditDefaultSwap(
            Protection::Buyer, 100.0, 0.01, schedule_, paymentConvention_,
            dayCounter_, settlesAccrual_, paysAtDefaultTime_, protectionStart_,
            boost::shared_ptr<Claim>(), lastPeriodDC_, rebatesAccrual_));

        if (useIsdaEngine_) {
            swap_->setPricingEngine(boost::make_shared<IsdaCdsEngine>(
                probability_, recoveryRate_, discountCurve_, false,
                static_cast<IsdaCdsEngine::NumericalFix>(isdaNumericalFix_),
                static_cast<IsdaCdsEngine::AccrualBias>(isdaAccrualBias_),
                static_cast<IsdaCdsEngine::ForwardsInCouponPeriod>(
                    isdaForwardsInCouponPeriod_)));
        } else {
            swap_->setPricingEngine(boost::make_shared<MidPointCdsEngine>(
                probability_, recoveryRate_, discountCurve_));
        }
    }

    UpfrontCdsHelper::UpfrontCdsHelper(
                              const Handle<Quote>& upfront,
                              Rate runningSpread,
                              const Period& tenor,
                              Integer settlementDays,
                              const Calendar& calendar,
                              Frequency frequency,
                              BusinessDayConvention paymentConvention,
                              DateGeneration::Rule rule,
                              const DayCounter& dayCounter,
                              Real recoveryRate,
                              const Handle<YieldTermStructure>& discountCurve,
                              Natural upfrontSettlementDays,
                              bool settlesAccrual,
                              bool paysAtDefaultTime,
                              const DayCounter& lastPeriodDayCounter,
                              const bool rebatesAccrual,
                              const bool useIsdaEngine)
    : CdsHelper(upfront, tenor, settlementDays, calendar,
                frequency, paymentConvention, rule, dayCounter,
                recoveryRate, discountCurve, settlesAccrual,
                paysAtDefaultTime, lastPeriodDayCounter, rebatesAccrual,
                useIsdaEngine),
      upfrontSettlementDays_(upfrontSettlementDays),
      runningSpread_(runningSpread) {
        initializeDates();
    }

    UpfrontCdsHelper::UpfrontCdsHelper(
                              Rate upfrontSpread,
                              Rate runningSpread,
                              const Period& tenor,
                              Integer settlementDays,
                              const Calendar& calendar,
                              Frequency frequency,
                              BusinessDayConvention paymentConvention,
                              DateGeneration::Rule rule,
                              const DayCounter& dayCounter,
                              Real recoveryRate,
                              const Handle<YieldTermStructure>& discountCurve,
                              Natural upfrontSettlementDays,
                              bool settlesAccrual,
                              bool paysAtDefaultTime,
                              const DayCounter& lastPeriodDayCounter,
                              const bool rebatesAccrual,
                              const bool useIsdaEngine)
    : CdsHelper(upfrontSpread, tenor, settlementDays, calendar,
                frequency, paymentConvention, rule, dayCounter,
                recoveryRate, discountCurve, settlesAccrual,
                paysAtDefaultTime, lastPeriodDayCounter, rebatesAccrual,
                useIsdaEngine),
      upfrontSettlementDays_(upfrontSettlementDays),
      runningSpread_(runningSpread) {
        initializeDates();
    }

    void UpfrontCdsHelper::initializeDates() {
        CdsHelper::initializeDates();
        upfrontDate_ = calendar_.advance(evaluationDate_,
                                         upfrontSettlementDays_, Days,
                                         paymentConvention_);
    }

    void UpfrontCdsHelper::resetEngine() {
        swap_ = boost::shared_ptr<CreditDefaultSwap>(new CreditDefaultSwap(
            Protection::Buyer, 100.0, 0.01, runningSpread_, schedule_,
            paymentConvention_, dayCounter_, settlesAccrual_,
            paysAtDefaultTime_, protectionStart_, upfrontDate_,
            boost::shared_ptr<Claim>(), lastPeriodDC_, rebatesAccrual_));
        if (useIsdaEngine_) {
            swap_->setPricingEngine(boost::make_shared<IsdaCdsEngine>(
                probability_, recoveryRate_, discountCurve_, false,
                static_cast<IsdaCdsEngine::NumericalFix>(isdaNumericalFix_),
                static_cast<IsdaCdsEngine::AccrualBias>(isdaAccrualBias_),
                static_cast<IsdaCdsEngine::ForwardsInCouponPeriod>(
                    isdaForwardsInCouponPeriod_)));
        } else {
            swap_->setPricingEngine(boost::make_shared<MidPointCdsEngine>(
                probability_, recoveryRate_, discountCurve_));
        }
    }

    Real UpfrontCdsHelper::impliedQuote() const {
        SavedSettings backup;
        Settings::instance().includeTodaysCashFlows() = true;
        swap_->recalculate();
        return swap_->fairUpfront();

    }

}
