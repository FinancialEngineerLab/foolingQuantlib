/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2015 Peter Caspers, Roland Lichters

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

#include <ql/experimental/models/betaetacore.hpp>

#include <ql/errors.hpp>
#include <ql/math/modifiedbessel.hpp>
#include <ql/math/integrals/segmentintegral.hpp>
#include <ql/math/integrals/gausslobattointegral.hpp>
#include <ql/math/integrals/trapezoidintegral.hpp>
#include <ql/methods/finitedifferences/meshers/concentrating1dmesher.hpp>

#include <boost/math/special_functions/gamma.hpp>
#include <boost/make_shared.hpp>

#include <ql/experimental/models/betaetatabulation.cpp>

#include <iostream>

namespace QuantLib {

BetaEtaCore::BetaEtaCore(const Array &times, const Array &alpha,
                         const Array &kappa, const Real &beta, const Real &eta)
    : times_(times), alpha_(alpha), kappa_(kappa), beta_(beta), eta_(eta),
      integrateStdDevs_(8.0) {

    QL_REQUIRE(beta > 0.0, "beta (" << beta << ") must be positive");
    QL_REQUIRE(eta >= 0.0 && eta <= 1.0, " eta (" << eta
                                                  << ") must be in [0,1]");
    QL_REQUIRE(alpha.size() == times.size() + 1,
               "alpha size (" << alpha.size()
                              << ") must be equal to times size ("
                              << times.size() << ") plus one");
    QL_REQUIRE(kappa.size() == 1 || kappa.size() == times.size() + 1,
               "kappa size (" << kappa.size()
                              << ") must be equal to times size ("
                              << times.size() << ") plus one or equal to one");
    for (Size i = 0; i < times.size(); ++i) {
        QL_REQUIRE(times[i] > 0.0, "time #" << i << " (" << times[i]
                                            << ") must be positive");
        if (i < times.size() - 1) {
            QL_REQUIRE(times[i] < times[i + 1],
                       "times must be strictly increasing, #"
                           << i << " and #" << (i + 1) << " are " << times[i]
                           << " and " << times[i + 1] << " respectively");
        }
    }

    // integrator to compute M directly in terms of x
    integrator_ = boost::make_shared<GaussLobattoIntegral>(100000, 1E-8, 1E-8);
    // integrator to compute M for case eta = 1
    ghIntegrator_ = boost::make_shared<GaussHermiteIntegration>(16);

    // integrator and fallback to precompute M
    preIntegrator_ =
        boost::make_shared<GaussLobattoIntegral>(100000, 1E-8, 1E-8);
    preIntegrator2_ = boost::make_shared<SegmentIntegral>(1000);

    // set up pretabulation coordinate data
    etaSize_ = sizeof(detail::eta_pre) / sizeof(detail::eta_pre[0]);
    uSize_ = sizeof(detail::u_pre) / sizeof(detail::u_pre[0]);
    vSize_ = sizeof(detail::v_pre) / sizeof(detail::v_pre[0]);
    eta_pre_ = std::vector<Real>(detail::eta_pre, detail::eta_pre + etaSize_);
    u_pre_ = std::vector<Real>(detail::u_pre, detail::u_pre + uSize_);
    v_pre_ = std::vector<Real>(detail::v_pre, detail::v_pre + vSize_);
};

// integrand to compute M directly in terms of x

class BetaEtaCore::mIntegrand1 {
    const BetaEtaCore *model_;
    const Real t0_, x0_, t_;

  public:
    mIntegrand1(const BetaEtaCore *model, const Real t0, const Real x0,
                const Real t)
        : model_(model), t0_(t0), x0_(x0), t_(t) {}
    Real operator()(Real x) const {
        return model_->p(t0_, x0_, t_, x) *
               exp(-model_->lambda(t_) * (x - x0_));
    }
};

// integrand to precompute M, 0 < eta < 1, eta != 0.5

class BetaEtaCore::mIntegrand2 {
    const BetaEtaCore *model_;
    const Real v_, u0_;
    const bool onePlusBetaX0Pos_, onePlusBetaXPos_;

  public:
    mIntegrand2(const BetaEtaCore *model, const Real v, const Real u0,
                const bool onePlusBetaX0Pos, const bool onePlusBetaXPos)
        : model_(model), v_(v), u0_(u0), onePlusBetaX0Pos_(onePlusBetaX0Pos),
          onePlusBetaXPos_(onePlusBetaXPos) {}
    Real operator()(Real u) const {
        if (close(u, 0))
            return 0.0; // is that reasonable or shall we interpolate ?
        Real eta = model_->eta();
        Real s0 = 1.0; // onePlusBetaX0Pos_ ? 1.0 : -1.0;
        Real s = 1.0;  // onePlusBetaXPos_ ? 1.0 : -1.0;
        Real res;
        res = s *
              model_->p_y_core(
                  v_, std::pow(u0_, 1.0 - eta) * std::pow(1.0 - eta, eta - 1.0),
                  std::pow(u, 1.0 - eta) * std::pow(1.0 - eta, eta - 1.0)) *
              std::exp(-(s * u - s0 * u0_));
        return res;
    }
};

// integrand to compute M, eta = 1

class BetaEtaCore::mIntegrand3 {
    const BetaEtaCore *model_;
    const Real v_, y0_, lambda_;

  public:
    mIntegrand3(const BetaEtaCore *model, const Real v, const Real y0,
                const Real lambda)
        : model_(model), v_(v), y0_(y0), lambda_(lambda) {}
    Real operator()(Real z) const {
        Real beta = model_->beta();
        Real y = M_SQRT2 * std::sqrt(v_) * z + y0_ - beta * v_ / 2.0;
        return exp(-lambda_ * (exp(beta * y) - exp(beta * y0_)) / beta) *
               exp(-z * z);
    }
};

const Real BetaEtaCore::M(const Time t0, const Real x0, const Real t,
                          const bool usePrecomputedValues) const {

    // since we are assuming a reflecting barrier we can
    // return M = 0 for all eta when y is negative or zero

    if (x0 <= -1.0 / beta_)
        return 0.0;
    Real lambda = this->lambda(t);
    Real v = this->tau(t0, t);

    // for zero volatility we obviously have M = 0

    if (close(v, 0.0))
        return 0.0;

    // without the reflecting barrier at y=0 we could write
    // M = 0.5 *lambda * lambda * v for eta = 0
    // with the reflecting barrier assumed here, we do not
    // have a closed form solution

    if (close(eta_, 0.5)) {
        return M_eta_05(t0, x0, t);
    }
    if (close(eta_, 1.0)) {
        return M_eta_1(t0, x0, t);
    }

    Real result1;
    if (usePrecomputedValues) {
        result1 = M_precomputed(t0, x0, t);
    } else {
        // determine a suitable integration domain
        Real s = std::sqrt(tau(t0, t));
        if (close(s, 0.0))
            return 0.0;
        Real a = std::max(x0 - integrateStdDevs_ * s, -1.0 / beta_);
        Real b = x0 + integrateStdDevs_ * s;
        result1 = integrator_->operator()(mIntegrand1(this, t0, x0, t), a, b);
        result1 = std::log(result1);
    }
    Real result2 =
        singularTerm_y_0(t0, x0, t) * exp(-lambda * (-1.0 / beta_ - x0));
    if (result2 > std::exp(result1) * QL_EPSILON) {
        return std::log(std::exp(result1) + result2);
    } else
        return result1;
};

const Real BetaEtaCore::M_eta_1(const Real t0, const Real x0,
                                const Real t) const {
    if (x0 < -1.0 / beta_)
        return 0.0;
    Real lambda = this->lambda(t);

    // eta may be not one, so we can not use the member function y

    Real y0 = std::log(1.0 + beta_ * x0) / beta_;
    Real v = this->tau(t0, t);
    Real result = M_1_SQRTPI *
                  ghIntegrator_->operator()(mIntegrand3(this, v, y0, lambda));
    return std::log(result);
}

const Real BetaEtaCore::M_eta_05(const Real t0, const Real x0,
                                 const Real t) const {
    if (x0 < -1.0 / beta_)
        return 0.0;
    Real lambda = this->lambda(t);
    Real v = this->tau(t0, t);
    return (1.0 + beta_ * x0) * lambda * lambda * v /
           (2.0 + beta_ * lambda * v);
}

const Real BetaEtaCore::M_precomputed(const Real t0, const Real x0,
                                      const Real t) const {

    Real v = this->tau(t0, t);
    Real lambda = this->lambda(t);

    if (close(eta_, 0.5) || close(eta_, 1.0)) {
        return M(t0, x0, t);
    }

    Real stddev = std::sqrt(v);
    Real xmax = x0 + integrateStdDevs_ * stddev;
    Real xmin = std::max(x0 - integrateStdDevs_ * stddev, -1.0 / beta_);
    Real smin = 1.0 + beta_ * xmin >= 0.0 ? 1.0 : -1.0;
    Real smax = 1.0 + beta_ * xmax >= 0.0 ? 1.0 : -1.0;
    Real u0 = lambda / beta_ * std::fabs(1.0 + beta_ * x0);
    std::cerr << "u0 = " << u0 << " lambda=" << lambda << " beta=" << beta_
              << std::endl;
    Real umin = lambda / beta_ * std::fabs(1.0 + beta_ * xmin);
    Real umax = lambda / beta_ * std::fabs(1.0 + beta_ * xmax);

    // read pretabulated value

    int etaIdx = std::upper_bound(eta_pre_.begin(), eta_pre_.end(), eta_) -
                 eta_pre_.begin();
    int uIdx =
        std::upper_bound(u_pre_.begin(), u_pre_.end(), u0) - u_pre_.begin();

    Real etamin = eta_pre_[etaIdx - 1];
    Real etamax =
        etaIdx < static_cast<int>(eta_pre_.size()) ? eta_pre_[etaIdx] : 1.0;

    std::cerr << "etamin = " << etamin << " etamax = " << etamax << std::endl;

    Real vt =
        v * std::pow(lambda, 2.0 - 2.0 * eta_) * std::pow(beta_, 2.0 * eta_);
    Real vtmin = v * std::pow(lambda, 2.0 - 2.0 * etamin) *
                 std::pow(beta_, 2.0 * etamin);
    Real vtmax = v * std::pow(lambda, 2.0 - 2.0 * etamax) *
                 std::pow(beta_, 2.0 * etamax);

    int vminIdx =
        std::upper_bound(v_pre_.begin(), v_pre_.end(), vtmin) - v_pre_.begin();
    int vmaxIdx =
        std::upper_bound(v_pre_.begin(), v_pre_.end(), vtmax) - v_pre_.begin();

    std::cerr << "vt=" << vt << " vtmin=" << vtmin << " vtmax=" << vtmax
              << " index=" << vminIdx << std::endl;

    int uIdx_low, uIdx_high;
    if (uIdx == 0) {
        uIdx_low = 0;
        uIdx_high = 1;
    } else {
        uIdx_high = std::min(uIdx, static_cast<int>(u_pre_.size()) - 1);
        uIdx_low = uIdx_high - 1;
    }

    std::cerr << "uindex = " << uIdx_low << " etaindex=" << etaIdx << std::endl;

    int vminIdx_low = std::max(vminIdx - 1, 0);
    int vminIdx_high = vminIdx_low + 1;

    int vmaxIdx_low = std::max(vmaxIdx - 1, 0);
    int vmaxIdx_high = vmaxIdx_low + 1;

    Real u_weight_1 =
        (u_pre_[uIdx_high] - u0) / (u_pre_[uIdx_high] - u_pre_[uIdx_low]);
    Real u_weight_2 =
        (u0 - u_pre_[uIdx_low]) / (u_pre_[uIdx_high] - u_pre_[uIdx_low]);

    Real vmin_weight_1 = (v_pre_[vminIdx_high] - vtmin) /
                         (v_pre_[vminIdx_high] - v_pre_[vminIdx_low]);
    Real vmin_weight_2 = (vtmin - v_pre_[vminIdx_low]) /
                         (v_pre_[vminIdx_high] - v_pre_[vminIdx_low]);
    Real vmax_weight_1 = (v_pre_[vmaxIdx_high] - vtmax) /
                         (v_pre_[vmaxIdx_high] - v_pre_[vmaxIdx_low]);
    Real vmax_weight_2 = (vtmax - v_pre_[vmaxIdx_low]) /
                         (v_pre_[vmaxIdx_high] - v_pre_[vmaxIdx_low]);

    std::cerr << "vmin weights=" << vmin_weight_1 << "," << vmin_weight_2
              << " for " << v_pre_[vminIdx_low] << " and "
              << v_pre_[vminIdx_high] << std::endl;

    Real eta_weight_1 =
        (etaIdx < static_cast<int>(eta_pre_.size()) ? eta_pre_[etaIdx] - eta_
                                                    : 1.0 - eta_) /
        (etaIdx < static_cast<int>(eta_pre_.size())
             ? (eta_pre_[etaIdx] - eta_pre_[etaIdx - 1])
             : (1.0 - eta_pre_[etaIdx - 1]));

    Real eta_weight_2 = (eta_ - eta_pre_[etaIdx - 1]) /
                        (etaIdx < static_cast<int>(eta_pre_.size())
                             ? (eta_pre_[etaIdx] - eta_pre_[etaIdx - 1])
                             : (1.0 - eta_pre_[etaIdx - 1]));

    Real result_eta_lower = detail::M_pre[etaIdx - 1][uIdx_low][vminIdx_low] *
                                u_weight_1 * vmin_weight_1 +
                            detail::M_pre[etaIdx - 1][uIdx_low][vminIdx_high] *
                                u_weight_1 * vmin_weight_2 +
                            detail::M_pre[etaIdx - 1][uIdx_high][vminIdx_low] *
                                u_weight_2 * vmin_weight_1 +
                            detail::M_pre[etaIdx - 1][uIdx_high][vminIdx_high] *
                                u_weight_2 * vmin_weight_2;

    std::cerr << "Interpolation in v direction: "
              << detail::M_pre[etaIdx - 1][uIdx_low][vminIdx_low] << " and "
              << detail::M_pre[etaIdx - 1][uIdx_low][vminIdx_high] << std::endl;

    Real result_eta_higher;
    if (etaIdx < static_cast<int>(eta_pre_.size())) {
        result_eta_higher = detail::M_pre[etaIdx][uIdx_low][vmaxIdx_low] *
                                u_weight_1 * vmax_weight_1 +
                            detail::M_pre[etaIdx][uIdx_low][vmaxIdx_high] *
                                u_weight_1 * vmax_weight_2 +
                            detail::M_pre[etaIdx][uIdx_high][vmaxIdx_low] *
                                u_weight_2 * vmax_weight_1 +
                            detail::M_pre[etaIdx][uIdx_high][vmaxIdx_high] *
                                u_weight_2 * vmax_weight_2;
    } else {
        result_eta_higher = M_eta_1(t0, x0, t);
    }

    Real resultm = 0.0;

    std::cerr << "result lookup (low=" << result_eta_lower
              << ", high=" << result_eta_higher
              << ") = " << ((result_eta_lower * eta_weight_1 +
                             result_eta_higher * eta_weight_2))
              << std::endl;

    // // integration (debug)
    Real result = 0.0;
    Real a = std::min(umin, umax);
    Real b = std::max(umin, umax);
    mIntegrand2 ig(this, vt, u0, 1.0 + beta_ * x0 >= 0.0, true);
    if (smin > 0.0 && smax > 0.0) {
        try {
            result += preIntegrator_->operator()(ig, a, b); // type 1 limits
        } catch (...) {
            result += preIntegrator2_->operator()(ig, a, b); // type 1 limits
        }
    }
    result = std::log(result);
    std::cerr << "result pre =" << result << " + " << resultm << std::endl;
    std::cerr << "result precomp(" << u0 << "," << vt
              << ") = " << precompute(u0, vt) << std::endl;
    return resultm + result;
    return 0.0;
}

const Real BetaEtaCore::precompute(const Real u0, const Real vt) const {
    Real res = 0.0;
    Real m = 1.3;
    if (close(vt, 0.0)) {
        res = 1.0;
    } else {
        // Real vth = vt + h;
        // Real vth2 = vt + 2.0 * h;
        mIntegrand2 ig(this, vt * std::pow(1.0 - eta_, 2.0 * eta_), u0, true,
                       true);
        // mIntegrand2 iguh(this, vt, u0h, true, true);
        // mIntegrand2 iguh2(this, vt, u0h2, true, true);
        // mIntegrand2 igvth(this, vth, u0, true, true);
        // mIntegrand2 igvth2(this, vth2, u0, true, true);
        Real a = u0;
        Real b = u0;
        while (ig(b) > 1E-10) {
            b *= m;
        }
        while (ig(a) > 1E-10 && a > 1E-10) {
            a /= m;
        }
        // Real res_u = 0.0;
        // Real res_vt = 0.0;
        // Real res_u2 = 0.0;
        // Real res_vt2 = 0.0;
        try {
            res += preIntegrator_->operator()(ig, a, b);
            // res_u += preIntegrator_->operator()(iguh, a, b);
            // res_vt += preIntegrator_->operator()(igvth, a, b);
            // res_u2 += preIntegrator_->operator()(iguh2, a, b);
            // res_vt2 += preIntegrator_->operator()(igvth2, a, b);
        } catch (...) {
        }
        if (res < 1e-10) {
            res += preIntegrator2_->operator()(ig, a, b);
            // res_u += preIntegrator2_->operator()(iguh, a, b);
            // res_vt += preIntegrator2_->operator()(igvth, a, b);
            // res_u2 += preIntegrator2_->operator()(iguh2, a, b);
            // res_vt2 += preIntegrator2_->operator()(igvth2, a, b);
        }
    }
    Real lres = std::log(res);
    return lres;
}

const void BetaEtaCore::precompute(const Real u0_min, const Real u0_max,
                                   const Real vt_min, const Real vt_max,
                                   const Size usize, const Size vsize,
                                   const Size etasteps, const Real cu,
                                   const Real densityu, const Real cv,
                                   const Real densityv, const Real ce,
                                   const Real densitye) {

    Concentrating1dMesher um(u0_min, u0_max, usize,
                             std::make_pair(cu, densityu), true);
    Concentrating1dMesher vm(vt_min, vt_max, vsize,
                             std::make_pair(cv, densityv), true);
    Concentrating1dMesher em(0.0, 1.0, etasteps, std::make_pair(ce, densitye),
                             true);

    std::cout << std::setprecision(8);
    std::cout << "// this file was generated by BetaEtaCore::precompute using "
                 "the following parameters:\n";
    std::cout << "// u0_min = " << u0_min << " u0_max = " << u0_max << "\n";
    std::cout << "// vt_min = " << vt_min << " vt_max = " << vt_max << "\n";
    std::cout << "// usize = " << usize << " vsize = " << vsize
              << " etaSteps = " << etasteps << "\n";
    std::cout << "// cu = " << cu << " densityu = " << densityu << "\n";
    std::cout << "// cv = " << cv << " densityv = " << densityv << "\n";
    std::cout << "// ce = " << ce << " densitye = " << densitye << "\n\n";
    std::cout << "namespace QuantLib {\n"
              << "namespace detail {\n\n";
    std::cout << "const Real eta_pre[] = {";
    for (Size i = 0; i < em.size() - 1; ++i)
        std::cout << em.location(i) << (i < em.size() - 2 ? "," : "};\n\n");
    std::cout << "const Real u_pre[] = {";
    for (Size i = 0; i < um.size(); ++i)
        std::cout << um.location(i) << (i < um.size() - 1 ? "," : "};\n\n");
    std::cout << "const Real v_pre[] = {";
    for (int i = -1; i < static_cast<int>(vm.size()); ++i)
        std::cout << (i == -1 ? 0.0 : vm.location(i))
                  << (i < static_cast<int>(vm.size()) - 1 ? "," : "};\n\n");

    std::cout << "const Real M_pre[][" << um.size() << "][" << (vm.size() + 1)
              << "] = {\n";
    // Real h = 0.01;
    Real etaSave = eta_;
    for (Size e = 0; e < etasteps - 1; ++e) {
        eta_ = em.location(e);
        std::cout << "// ========================  eta=" << eta_ << "\n";
        std::cout << "{ ";
        for (Size i = 0; i < um.size(); ++i) {
            Real u0 = um.location(i);
            std::cout << "// eta=" << eta_ << " u=" << u0 << "\n";
            std::cout << "{";
            for (int j = -1; j < static_cast<int>(vm.size()); ++j) {
                // Real u0h = u0 + h;
                // Real u0h2 = u0 + 2.0 * h;
                Real vt = j == -1 ? 0.0 : vm.location(j);
                // Real lres_u =
                //     (std::log(res_u2) - 2.0 * std::log(res_u) +
                //     std::log(res)) /
                //     (h * h);
                // Real lres_vt = (std::log(res_vt2) - 2.0 * std::log(res_vt) +
                //                 std::log(res)) /
                //                (h * h);
                // std::cout << u0 << " " << vt << " " << res << " " << lres <<
                // " "
                //           << lres_u << " " << lres_vt << std::endl;
                Real lres = precompute(u0, vt);
                std::cout << lres
                          << (j < static_cast<int>(vm.size()) - 1 ? "," : "}");
                // std::cout << u0 << " " << vt << " " << lres << std::endl;
            }
            std::cout << (i < um.size() - 1 ? ",\n" : "}");
            // std::cout << std::endl;
        }
        std::cout << (e < etasteps - 2 ? ",\n" : "};\n");
    }
    std::cout << "} // namespace detail\n"
              << "} // namespace QuantLib\n";
    eta_ = etaSave;
}

const Real BetaEtaCore::p_y_core(const Real v, const Real y0,
                                 const Real y) const {
    QL_REQUIRE(!close(eta_, 1.0), "eta must not be one in p_y_core");
    if (close(y, 0.0) || close(y0, 0.0)) // i.e. x, x0 = -1/beta
        return 0.0;
    Real nu = 1.0 / (2.0 - 2.0 * eta_);
    // 0.0 <= eta < 0.5
    if (eta_ < 0.5) {
        return std::pow(y0 / y, nu) * y / v *
               modifiedBesselFunction_i_exponentiallyWeighted(-nu, y0 * y / v) *
               exp(-(y - y0) * (y - y0) / (2.0 * v)) *
               std::pow(y, eta_ / (eta_ - 1.0));
    }
    // 0.5 <= eta < 1.0

    return std::pow(y0 / y, nu) * y / v *
           modifiedBesselFunction_i_exponentiallyWeighted(nu, y0 * y / v) *
           exp(-(y - y0) * (y - y0) / (2.0 * v)) *
           std::pow(y, eta_ / (eta_ - 1.0));
}

const Real BetaEtaCore::p_y(const Real v, const Real y0, const Real y,
                            const bool onePlusBetaXPos) const {
    // eta = 1.0
    if (close(eta_, 1.0)) {
        return exp(-beta_ * y) / std::sqrt(2.0 * M_PI * v) *
               exp(-0.5 * (y - y0 + 0.5 * beta_ * v) *
                   (y - y0 + 0.5 * beta_ * v) / v);
    }
    // eta < 1.0
    return p_y_core(v, y0, y) * std::pow(1.0 - eta_, eta_ / (eta_ - 1.0)) *
           std::pow(beta_, eta_ / (eta_ - 1.0));
}

const Real BetaEtaCore::p(const Time t0, const Real x0, const Real t,
                          const Real x) const {
    if (x <= -1.0 / beta_)
        return 0.0;
    Real v = this->tau(t0, t);
    Real y0 = this->y(x0);
    Real y = this->y(x);
    return p_y(v, y0, y, 1.0 + beta_ * x > 0);
};

const Real BetaEtaCore::singularTerm_y_0(const Time t0, const Real x0,
                                         const Time t) const {
    if (eta_ < 0.5 || close(eta_, 1.0))
        return 0.0;
    Real nu = 1.0 / (2.0 - 2.0 * eta_);
    Real y0 = this->y(x0);
    Real tau0 = this->tau(t0);
    Real tau = this->tau(t);
    return boost::math::gamma_q(nu, y0 * y0 / (2.0 * (tau - tau0)));
};

} // namespace QuantLib
