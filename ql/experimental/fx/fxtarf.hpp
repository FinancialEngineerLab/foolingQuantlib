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

/*! \file fxtarf.hpp
    \brief FX TARF instrument
*/

#ifndef quantlib_fxtarf_hpp
#define quantlib_fxtarf_hpp

#include <ql/experimental/fx/fxindex.hpp>
#include <ql/experimental/fx/proxyinstrument.hpp>
#include <ql/instrument.hpp>
#include <ql/time/schedule.hpp>
#include <ql/option.hpp>
#include <ql/instruments/payoffs.hpp>

namespace QuantLib {

class FxTarf : public Instrument, public ProxyInstrument {
  public:
    //! coupon types
    enum CouponType { none, capped, full };
    //! forward declarations
    class arguments;
    class results;
    class engine;
    //! proxy description
    struct Proxy : ProxyDescription {
        bool dummy; // stub implementation
    };
    //! \name Constructors
    //@{
    /*! If the accumulatedAmount is not null, no past fixings are
        used to calculate the accumulated amount, but exactly this
        number is assumed to represent this amount. The last amount
        must then be fixed to the last fixed amount in order to
        get consistent npvs between fixing and payment date.
        Note that the accumulatedAmount should always assume a full
        coupon (this is only used to check the target trigger and
        the coupon type none would lead to false results then).
    */
    FxTarf(const Schedule schedule, const boost::shared_ptr<FxIndex> &index,
           const Real sourceNominal,
           const boost::shared_ptr<StrikedTypePayoff> &shortPositionPayoff,
           const boost::shared_ptr<StrikedTypePayoff> &longPositionPayoff,
           const Real target, const CouponType couponType = capped,
           const Real shortPositionGearing = 1.0,
           const Real longPositionGearing = 1.0,
           const Handle<Quote> accumulatedAmount = Handle<Quote>(),
           const Handle<Quote> lastAmount = Handle<Quote>());
    //@}
    //! \name Instrument interface
    //@{
    // the tarf is expired iff accumulated amount >= target
    // and this amount is settled
    bool isExpired() const;
    void setupArguments(PricingEngine::arguments *) const;
    void fetchResults(const PricingEngine::results *) const;
    //@}
    //! \name Additional interface
    //@{
    Date startDate() const;
    Date maturityDate() const;
    /*! this is the accumulated amount, but always assuming
        the coupon type full
     */
    Real accumulatedAmount() const {
        return accumulatedAmountAndSettlement().first;
    }
    Real lastAmount() const;
    bool lastAmountSettled() const {
        return accumulatedAmountAndSettlement().second;
    }
    Real target() const { return target_; }
    Real sourceNominal() const { return sourceNominal_; }
    //! description for proxy pricing
    boost::shared_ptr<ProxyDescription> proxy() const;
    /*! payout in domestic currency (for nominal 1) */
    Real payout(const Real fixing) const;
    /*! same as above, but assuming the given accumulated amount,
        which is in addition updated to the new value after the
        fixing */
    Real payout(const Real fixing, Real &accumulatedAmount) const;

  protected:
    //! \name Instrument interface
    //@{
    void setupExpired() const;
    //@}

  private:
    /* payout assuming a full coupon and the given accumulated amount,
       which is updated at the same time (for nominal 1) */
    Real nakedPayout(const Real fixing, Real &accumulatedAmount) const;
    std::pair<Real, bool> accumulatedAmountAndSettlement() const;
    // termsheet data
    const Schedule schedule_;
    const boost::shared_ptr<FxIndex> index_;
    const Real sourceNominal_;
    const boost::shared_ptr<StrikedTypePayoff> shortPositionPayoff_,
        longPositionPayoff_;
    const Real target_;
    const CouponType couponType_;
    const Real shortPositionGearing_, longPositionGearing_;
    // additional data
    std::vector<Date> openFixingDates_, openPaymentDates_;
    Handle<Quote> accumulatedAmount_, lastAmount_;
    // proxy pricing information
    mutable boost::shared_ptr<ProxyDescription> proxy_;
};

class FxTarf::arguments : public virtual PricingEngine::arguments {
  public:
    Schedule schedule;
    std::vector<Date> openFixingDates, openPaymentDates;
    boost::shared_ptr<FxIndex> index;
    Real target, sourceNominal;
    Real accumulatedAmount, lastAmount;
    const FxTarf *instrument;
    void validate() const;
};

class FxTarf::results : public Instrument::results {
  public:
    void reset();
    boost::shared_ptr<FxTarf::Proxy> proxy;
};

class FxTarf::engine
    : public GenericEngine<FxTarf::arguments, FxTarf::results> {};

} // namespace QuantLib

#endif
