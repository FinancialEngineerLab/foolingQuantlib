/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
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

/*! \file sabrinterpolation.hpp
    \brief SVI interpolation interpolation between discrete points
*/

#ifndef quantlib_svi_interpolation_hpp
#define quantlib_svi_interpolation_hpp

#include <ql/math/interpolations/xabrinterpolation.hpp>

#include <boost/make_shared.hpp>
#include <boost/assign/list_of.hpp>

namespace QuantLib {

namespace detail {

class SVIWrapper {
  public:
    SVIWrapper(const Time t, const Real &forward,
               const std::vector<Real> &params)
        : t_(t), forward_(forward), params_(params) {
        QL_REQUIRE(params[1] >= 0.0, "b (" << params[1]
                   << ") must be non negative");
        QL_REQUIRE(std::fabs(params[3]) < 1.0,
                   "rho (" << params[3] << ") must be in (-1,1)");
        QL_REQUIRE(params[2] > 0.0, "sigma (" << params[2]
                                              << ") must be positive");
        QL_REQUIRE(params[0] +
                           params[1] * params[2] *
                               std::sqrt(1.0 - params[3] * params[3]) >=
                       0.0,
                   "a + bs sqrt(1-r^2) must be non negative");
        QL_REQUIRE(params[1]*(1.0+std::fabs(params[3])) < 4.0,"b(1+|r|) must be less than 4");
    }
    Real volatility(const Real x) {
        Real k = std::log(std::max(x, 1E-6) / forward_);
        Real totalVariance =
            params_[0] +
            params_[1] * (params_[3] * (k - params_[4]) +
                          std::sqrt((k - params_[4]) * (k - params_[4]) +
                                    params_[2] * params_[2]));
        return std::sqrt(std::max(0.0, totalVariance / t_));
    }

  private:
    const Real t_, &forward_;
    const std::vector<Real> &params_;
};

struct SVISpecs {
    Size dimension() { return 5; }
    void defaultValues(std::vector<Real> &params, std::vector<bool> &paramIsFixed,
                       const Real &forward, const Real expiryTime) {
        if (params[2] == Null<Real>())
            params[2] = 0.1;
        if (params[3] == Null<Real>())
            params[3] = -0.4;
        if (params[4] == Null<Real>())
            params[4] = 0.0;
        if (params[1] == Null<Real>())
            params[1] = 2.0 / (1.0 + std::fabs(params[3]));
        if (params[0] == Null<Real>())
            params[0] = 0.20 * 0.20 * expiryTime -
                        params[1] * (params[3] * (-params[4]) +
                                     std::sqrt((-params[4]) * (-params[4]) +
                                               params[2] * params[2]));
    }
    void guess(Array &values, const std::vector<bool> &paramIsFixed,
               const Real &forward, const Real expiryTime,
               const std::vector<Real> &r) {
        Size j = 0;
        if (!paramIsFixed[2])
            values[2] = r[j++] + eps1();
        if (!paramIsFixed[3])
            values[3] = (2.0 * r[j++] - 1.0) * eps2();
        if (!paramIsFixed[4])
            values[4] = (2.0 * r[j++] - 1.0);
        if (!paramIsFixed[1])
            values[1] = r[j++] * 4.0 / (1.0 + std::fabs(values[3])) * eps2();
        if (!paramIsFixed[0])
            values[0] = r[j++] -
                        eps2() * (values[1] * values[2] *
                                  std::sqrt(1.0 - values[3] * values[3]));
    }
    Array inverse(const Array &y, const std::vector<bool> &,
                  const std::vector<Real> &, const Real) {
        Array x(5);
        x[2] = std::sqrt(y[2] - eps1());
        x[3] = std::asin(y[3] / eps2());
        x[4] = y[4];
        x[1] = std::tan(y[1] / 4.0 * (1.0 + std::fabs(y[3])) / eps2() * M_PI -
                        M_PI / 2.0);
        x[0] = std::sqrt(y[0] +
                         eps2() * y[1] * y[2] * std::sqrt(1.0 - y[3] * y[3]));
        return x;
    }
    Real eps1() { return 0.000001; }
    Real eps2() { return 0.999999; }
    Array direct(const Array &x, const std::vector<bool> &paramIsFixed,
                 const std::vector<Real> &params, const Real forward) {
        Array y(5);
        y[2] = x[2] * x[2] + eps1();
        y[3] = std::sin(x[3] * eps2());
        y[4] = x[4];
        if (paramIsFixed[1])
            y[1] = params[1];
        else
            y[1] = (std::atan(x[1]) + M_PI / 2.0) / M_PI * eps2() * 4.0 /
                   (1.0 + std::fabs(y[3]));
        if (paramIsFixed[0])
            y[0] = params[0];
        else
            y[0] = x[0] * x[0] -
                   eps2() * y[1] * y[2] * std::sqrt(1.0 - y[3] * y[3]);
        return y;
    }
    typedef SVIWrapper type;
    boost::shared_ptr<type> instance(const Time t, const Real &forward,
                                     const std::vector<Real> &params) {
        return boost::make_shared<type>(t, forward, params);
    }
};
}

//! %SVI smile interpolation between discrete volatility points.
class SVIInterpolation : public Interpolation {
  public:
    template <class I1, class I2>
    SVIInterpolation(const I1 &xBegin, // x = strikes
                     const I1 &xEnd,
                     const I2 &yBegin, // y = volatilities
                     Time t,           // option expiry
                     const Real &forward, Real a, Real b, Real sigma, Real rho,
                     Real m, bool aIsFixed, bool bIsFixed, bool sigmaIsFixed,
                     bool rhoIsFixed, bool mIsFixed, bool vegaWeighted = true,
                     const boost::shared_ptr<EndCriteria> &endCriteria =
                         boost::shared_ptr<EndCriteria>(),
                     const boost::shared_ptr<OptimizationMethod> &optMethod =
                         boost::shared_ptr<OptimizationMethod>(),
                     const Real errorAccept = 0.0020,
                     const bool useMaxError = false,
                     const Size maxGuesses = 50) {

        impl_ = boost::shared_ptr<Interpolation::Impl>(
            new detail::XABRInterpolationImpl<I1, I2, detail::SVISpecs>(
                xBegin, xEnd, yBegin, t, forward,
                boost::assign::list_of(a)(b)(sigma)(rho)(m),
                boost::assign::list_of(aIsFixed)(bIsFixed)(sigmaIsFixed)(
                    rhoIsFixed)(mIsFixed),
                vegaWeighted, endCriteria, optMethod, errorAccept, useMaxError,
                maxGuesses));
        coeffs_ = boost::dynamic_pointer_cast<
            detail::XABRCoeffHolder<detail::SVISpecs> >(impl_);
    }
    Real expiry() const { return coeffs_->t_; }
    Real forward() const { return coeffs_->forward_; }
    Real a() const { return coeffs_->params_[0]; }
    Real b() const { return coeffs_->params_[1]; }
    Real sigma() const { return coeffs_->params_[2]; }
    Real rho() const { return coeffs_->params_[3]; }
    Real m() const { return coeffs_->params_[4]; }
    Real rmsError() const { return coeffs_->error_; }
    Real maxError() const { return coeffs_->maxError_; }
    const std::vector<Real> &interpolationWeights() const {
        return coeffs_->weights_;
    }
    EndCriteria::Type endCriteria() { return coeffs_->XABREndCriteria_; }

  private:
    boost::shared_ptr<detail::XABRCoeffHolder<detail::SVISpecs> > coeffs_;
};

//! %SVI interpolation factory and traits
class SVI {
  public:
    SVI(Time t, Real forward, Real a, Real b, Real sigma, Real rho, Real m,
         bool aIsFixed, bool bIsFixed, bool sigmaIsFixed, bool rhoIsFixed,
         bool mIsFixed, bool vegaWeighted = false,
         const boost::shared_ptr<EndCriteria> endCriteria =
             boost::shared_ptr<EndCriteria>(),
         const boost::shared_ptr<OptimizationMethod> optMethod =
             boost::shared_ptr<OptimizationMethod>(),
         const Real errorAccept = 0.0020, const bool useMaxError = false,
         const Size maxGuesses = 50)
        : t_(t), forward_(forward), a_(a), b_(b), sigma_(sigma), rho_(rho),
          m_(m), aIsFixed_(aIsFixed), bIsFixed_(bIsFixed),
          sigmaIsFixed_(sigmaIsFixed), rhoIsFixed_(rhoIsFixed),
          mIsFixed_(mIsFixed), vegaWeighted_(vegaWeighted),
          endCriteria_(endCriteria), optMethod_(optMethod),
          errorAccept_(errorAccept), useMaxError_(useMaxError),
          maxGuesses_(maxGuesses) {}
    template <class I1, class I2>
    Interpolation interpolate(const I1 &xBegin, const I1 &xEnd,
                              const I2 &yBegin) const {
        return SVIInterpolation(xBegin, xEnd, yBegin, t_, forward_, a_, b_,
                                 sigma_, rho_, m_, aIsFixed_, bIsFixed_,
                                 sigmaIsFixed_, rhoIsFixed_, mIsFixed_,
                                 vegaWeighted_, endCriteria_, optMethod_,
                                 errorAccept_, useMaxError_, maxGuesses_);
    }
    static const bool global = true;

  private:
    Time t_;
    Real forward_;
    Real a_, b_, sigma_, rho_, m_;
    bool aIsFixed_, bIsFixed_, sigmaIsFixed_, rhoIsFixed_, mIsFixed_;
    bool vegaWeighted_;
    const boost::shared_ptr<EndCriteria> endCriteria_;
    const boost::shared_ptr<OptimizationMethod> optMethod_;
    const Real errorAccept_;
    const bool useMaxError_;
    const Size maxGuesses_;
};
}

#endif
