/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2012 Peter Caspers

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

#include <ql/termstructures/volatility/zabr.hpp>
#include <ql/termstructures/volatility/sabr.hpp>
#include <ql/errors.hpp>
#include <ql/math/comparison.hpp>

#include <boost/function.hpp>
#include <boost/lambda/lambda.hpp>
//#include <boost/lambda/if.hpp>
#include <boost/lambda/bind.hpp>

#include <ql/experimental/math/adaptiverungekutta.hpp>

#include <ql/methods/finitedifferences/operators/fdmlinearoplayout.hpp>
#include <ql/methods/finitedifferences/meshers/fdm1dmesher.hpp>
#include <ql/methods/finitedifferences/meshers/uniform1dmesher.hpp>
#include <ql/methods/finitedifferences/meshers/concentrating1dmesher.hpp>
#include <ql/experimental/finitedifferences/glued1dmesher.hpp>
#include <ql/methods/finitedifferences/meshers/fdmmeshercomposite.hpp>
#include <ql/methods/finitedifferences/operatortraits.hpp>
#include <ql/methods/finitedifferences/utilities/fdmdirichletboundary.hpp>
#include <ql/methods/finitedifferences/solvers/fdmbackwardsolver.hpp>

#include <ql/experimental/finitedifferences/fdmdupire1dop.hpp>
#include <ql/experimental/finitedifferences/fdmzabrop.hpp>

namespace QuantLib {

    ZabrModel::ZabrModel(const Real expiryTime, const Real forward,
                         const Real alpha, const Real beta, const Real nu,
                         const Real rho, const Real gamma)
        : expiryTime_(expiryTime), forward_(forward), alpha_(alpha),
          beta_(beta), nu_(nu), rho_(rho), gamma_(gamma) {

        validateSabrParameters(alpha, beta, nu, rho);
        QL_REQUIRE(gamma >= 0.0 /*&& gamma<=1.0*/,
                   "gamma must be non negative: " << gamma << " not allowed");
        QL_REQUIRE(forward >= 0.0, "forward must be non negative: "
                                       << forward << " not allowed");
        QL_REQUIRE(expiryTime > 0.0, "expiry time must be positive: "
                                         << expiryTime << " not allowed");
    }

    Real ZabrModel::lognormalVolatilityHelper(const Real strike,
                                              const Real x) const {
        if (close(strike, forward_))
            return std::pow(forward_, beta_ - 1.0) * alpha_;
        else
            return std::log(forward_ / strike) / x;
    }

    Real ZabrModel::lognormalVolatility(const Real strike) const {
        return lognormalVolatility(std::vector<Real>(1, strike))[0];
    }

    Disposable<std::vector<Real> > ZabrModel::lognormalVolatility(const std::vector<Real> &strikes) const {
        std::vector<Real> x_ = x(strikes);
        std::vector<Real> result(strikes.size());
        std::transform(
            strikes.begin(), strikes.end(), x_.begin(), result.begin(),
            boost::lambda::bind(&ZabrModel::lognormalVolatilityHelper, this,
                                boost::lambda::_1, boost::lambda::_2));
        return result;
    }

    Real ZabrModel::normalVolatilityHelper(const Real strike,
                                           const Real x) const {
        if (close(strike, forward_))
            return std::pow(forward_, beta_) * alpha_;
        else
            return (forward_ - strike) / x;
    }

    Real ZabrModel::normalVolatility(const Real strike) const {
        return normalVolatility(std::vector<Real>(1, strike))[0];
    }

    Disposable<std::vector<Real> >
    ZabrModel::normalVolatility(const std::vector<Real> &strikes) const {
        std::vector<Real> x_ = x(strikes);
        std::vector<Real> result(strikes.size());
        std::transform(
            strikes.begin(), strikes.end(), x_.begin(), result.begin(),
            boost::lambda::bind(&ZabrModel::normalVolatilityHelper, this,
                                boost::lambda::_1, boost::lambda::_2));
        return result;
    }

    Real ZabrModel::localVolatilityHelper(const Real f, const Real x) const {
        //return (f+0.05) * 0.20; // debug shifted lognormal local vol with shift 0.05
        return alpha_ * std::pow(std::fabs(f), beta_) /
               F(y(f), std::pow(alpha_, gamma_ - 1.0) *
                           x); // TODO optimize this, y is comoputed together
                               // with x already
    }

    Real ZabrModel::localVolatility(const Real f) const {
        return localVolatility(std::vector<Real>(1, f))[0];
    }

    Disposable<std::vector<Real> >
    ZabrModel::localVolatility(const std::vector<Real> &f) const {
        std::vector<Real> x_ = x(f);
        std::vector<Real> result(f.size());
        std::transform(
            f.begin(), f.end(), x_.begin(), result.begin(),
            boost::lambda::bind(&ZabrModel::localVolatilityHelper, this,
                                boost::lambda::_1, boost::lambda::_2));
        return result;
    }

    Real ZabrModel::fdPrice(const Real strike) const {
        return fdPrice(std::vector<Real>(1, strike))[0];
    }

    Disposable<std::vector<Real> >
    ZabrModel::fdPrice(const std::vector<Real> &strikes) const {

        // TODO check strikes to be increasing

        // TODO put these magic numbers somewhere ...
        const Real start = std::min(-forward_ * 15.0, strikes.front()); // 0.00001; // lowest strike for grid
        const Real end = std::max(forward_ * 15.0, strikes.back()); // highest strike for grid
        const Size size = 500;           // grid points
        const Real density = 0.005;      // density for concentrating mesher
        const Size steps =
            (Size)std::ceil(expiryTime_ * 50); // number of steps in dimension t
        const Size dampingSteps = 50;          // thereof damping steps

        // Layout
        std::vector<Size> dim(1, size);
        const boost::shared_ptr<FdmLinearOpLayout> layout(
            new FdmLinearOpLayout(dim));
        // Mesher
        const boost::shared_ptr<Fdm1dMesher> m1(new Concentrating1dMesher(
            start, end, size, std::pair<Real, Real>(0.0, density), true));
        // const boost::shared_ptr<Fdm1dMesher> m1(new  Uniform1dMesher(start,end,size));
        const std::vector<boost::shared_ptr<Fdm1dMesher> > meshers(1, m1);
        const boost::shared_ptr<FdmMesher> mesher(
            new FdmMesherComposite(layout, meshers));
        // Boundary conditions
        FdmBoundaryConditionSet boundaries;
        // boundaries.push_back(boost::shared_ptr<BoundaryCondition<FdmLinearOp> >(
        //     new FdmDirichletBoundary(
        //         mesher, 0.0, 0, FdmDirichletBoundary::Upper))); // for strike =
        //                                                         // \infty call
        //                                                         // is worth zero
        // boundaries.push_back(boost::shared_ptr<BoundaryCondition<FdmLinearOp> >(
        //     new FdmDirichletBoundary(
        //         mesher, forward_ - start, 0,
        //         FdmDirichletBoundary::Lower))); // for strike = -\infty call is
        //                                         // worth f-k
        // initial values
        Array rhs(mesher->layout()->size());
        for (FdmLinearOpIterator iter = layout->begin(); iter != layout->end();
             ++iter) {
            Real k = mesher->location(iter, 0);
            rhs[iter.index()] = std::max(forward_ - k, 0.0);
        }
        // local vols (TODO how can we avoid these Array / vector copying?)
        Array k = mesher->locations(0);
        std::vector<Real> kv(k.size());
        std::copy(k.begin(), k.end(), kv.begin());
        std::vector<Real> locVolv = localVolatility(kv);
        Array locVol(locVolv.size());
        std::copy(locVolv.begin(), locVolv.end(), locVol.begin());
        // solver
        boost::shared_ptr<FdmDupire1dOp> map(new FdmDupire1dOp(mesher, locVol));
        FdmBackwardSolver solver(map, boundaries,
                                 boost::shared_ptr<FdmStepConditionComposite>(),
                                 FdmSchemeDesc::Douglas());
        solver.rollback(rhs, expiryTime_, 0.0, steps, dampingSteps);

        // interpolate solution
        boost::shared_ptr<Interpolation> solution(new CubicInterpolation(
            k.begin(), k.end(), rhs.begin(), CubicInterpolation::Spline, true,
            CubicInterpolation::SecondDerivative, 0.0,
            CubicInterpolation::SecondDerivative, 0.0));
        // boost::shared_ptr<Interpolation> solution(new
        // LinearInterpolation(k.begin(),k.end(),rhs.begin()));
        //solution->disableExtrapolation();
        solution->enableExtrapolation();
        // return solution;
        std::vector<Real> result(strikes.size());
        std::transform(strikes.begin(), strikes.end(), result.begin(),
                       *solution);
        return result;
    }

    Real ZabrModel::fullFdPrice(const Real strike) const {

        // TODO put these magic numbers somewhere ...
        const Real f0 = 0.00001, f1 = forward_ * 75; // forward
        const Real v0 = 0.00001, v1 = alpha_ * 75;   // volatility
        const Size sizef = 100, sizev = 100;         // grid points
        const Real densityf = 0.01,
                   densityv = 0.01; // density for concentrating mesher
        const Size steps =
            (Size)std::ceil(expiryTime_ * 50); // number of steps in dimension t
        const Size dampingSteps = 20;          // thereof damping steps

        QL_REQUIRE(strike >= f0 && strike <= f1,
                   "strike (" << strike << ") must be inside pde grid [" << f0
                              << ";" << f1 << "]");

        // Layout
        std::vector<Size> dim;
        dim.push_back(sizef);
        dim.push_back(sizev);
        const boost::shared_ptr<FdmLinearOpLayout> layout(
            new FdmLinearOpLayout(dim));
        // Mesher

        // two concentrating mesher around f and k to get the mesher for the
        // forward
        // const Real x0 = std::min(forward_,strike);
        // const Real x1 = std::max(forward_,strike);
        // const Size sizefa =
        // (Size)std::ceil(((x0+x1)/2.0-f0)/(f1-f0)*(Real)sizef);
        // const Size sizefb = sizef-sizefa+1; // common point, so we can spend
        // one more here
        // const boost::shared_ptr<Fdm1dMesher> mfa(new
        // Concentrating1dMesher(f0,(x0+x1)/2.0,sizefa,std::pair<Real,Real>(x0,densityf),true));
        // const boost::shared_ptr<Fdm1dMesher> mfb(new
        // Concentrating1dMesher((x0+x1)/2.0,f1,sizefb,std::pair<Real,Real>(x1,densityf),true));
        // const boost::shared_ptr<Fdm1dMesher> mf(new
        // Glued1dMesher(*mfa,*mfb));

        // concentraing mesher around k to get the forward mesher
        const boost::shared_ptr<Fdm1dMesher> mf(new Concentrating1dMesher(
            f0, f1, sizef, std::pair<Real, Real>(strike, densityf), true));

        // volatility mesher
        const boost::shared_ptr<Fdm1dMesher> mv(new Concentrating1dMesher(
            v0, v1, sizev, std::pair<Real, Real>(alpha_, densityv), true));

        // uniform meshers
        // const boost::shared_ptr<Fdm1dMesher> mf(new
        // Uniform1dMesher(f0,f1,sizef));
        // const boost::shared_ptr<Fdm1dMesher> mv(new
        // Uniform1dMesher(v0,v1,sizev));

        std::vector<boost::shared_ptr<Fdm1dMesher> > meshers;
        meshers.push_back(mf);
        meshers.push_back(mv);
        const boost::shared_ptr<FdmMesher> mesher(
            new FdmMesherComposite(layout, meshers));
        // initial values
        Array rhs(mesher->layout()->size());
        std::vector<Real> f_;
        std::vector<Real> v_;
        for (FdmLinearOpIterator iter = layout->begin(); iter != layout->end();
             ++iter) {
            Real f = mesher->location(iter, 0);
            //Real v = mesher->location(iter, 0);
            rhs[iter.index()] = std::max(f - strike, 0.0);
            if (!iter.coordinates()[1])
                f_.push_back(mesher->location(iter, 0));
            if (!iter.coordinates()[0])
                v_.push_back(mesher->location(iter, 1));
        }

        // Boundary conditions
        FdmBoundaryConditionSet boundaries;
        boost::shared_ptr<FdmDirichletBoundary> b_dirichlet(
            new FdmDirichletBoundary(
                mesher, f1 - strike, 0,
                FdmDirichletBoundary::Upper)); // put is worth zero for forward
                                               // = \infty
        boundaries.push_back(b_dirichlet);
        boost::shared_ptr<FdmZabrOp> map(
            new FdmZabrOp(mesher, beta_, nu_, rho_, gamma_));
        FdmBackwardSolver solver(map, boundaries,
                                 boost::shared_ptr<FdmStepConditionComposite>(),
                                 FdmSchemeDesc::CraigSneyd());

        solver.rollback(rhs, expiryTime_, 0.0, steps, dampingSteps);

        // interpolate solution (this is not necessary when using concentrating
        // meshers with required point)
        Matrix result(f_.size(), v_.size());
        for (Size j = 0; j < v_.size(); ++j)
            std::copy(rhs.begin() + j * f_.size(),
                      rhs.begin() + (j + 1) * f_.size(), result.row_begin(j));

        boost::shared_ptr<BicubicSpline> interpolation =
            boost::shared_ptr<BicubicSpline>(new BicubicSpline(
                f_.begin(), f_.end(), v_.begin(), v_.end(), result));
        interpolation->disableExtrapolation();
        return (*interpolation)(forward_, alpha_);
    }

    Real ZabrModel::x(const Real strike) const {
        return x(std::vector<Real>(1, strike))[0];
    }

    Disposable<std::vector<Real> > ZabrModel::x(const std::vector<Real> &strikes) const {

        QL_REQUIRE(strikes[0] > 0.0 || beta_ < 1.0, "strikes must be positive (" << strikes[0]
                                                                  << ") if beta = 1");
        for (std::vector<Real>::const_iterator i = strikes.begin() + 1;
             i != strikes.end(); i++)
            QL_REQUIRE(*i > *(i - 1), "strikes must be strictly ascending ("
                                          << *(i - 1) << "," << *i << ")");

        AdaptiveRungeKutta<Real> rk(1.0E-8, 1.0E-5,
                                    0.0); // TODO move the magic numbers here as
                                          // parameters with default values to
                                          // the constructor
        std::vector<Real> y(strikes.size()), result(strikes.size());
        std::transform(
            strikes.rbegin(), strikes.rend(), y.begin(),
            boost::lambda::bind(&ZabrModel::y, this, boost::lambda::_1));

        if (close(gamma_, 1.0)) {
            for (Size m = 0; m < y.size(); m++) {
                Real J = std::sqrt(1.0 + nu_ * nu_ * y[m] * y[m] -
                                   2.0 * rho_ * nu_ * y[m]);
                result[y.size() - 1 - m] =
                    std::log((J + nu_ * y[m] - rho_) / (1.0 - rho_)) / nu_;
            }
        } else {
            Size ynz = std::upper_bound(y.begin(), y.end(), 0.0) - y.begin();
            if (ynz > 0)
                if (close(y[ynz - 1], 0.0))
                    ynz--;
            if (ynz == y.size())
                ynz--;

            for (int dir = 1; dir >= -1; dir -= 2) {
                Real y0 = 0.0, u0 = 0.0;
                for (int m = ynz + (dir == -1 ? -1 : 0);
                     dir == -1 ? m >= 0 : m < (int)y.size(); m += dir) {
                    Real u = rk(boost::lambda::bind(&ZabrModel::F, this,
                                                    boost::lambda::_1,
                                                    boost::lambda::_2),
                                u0, y0, y[m]);
                    result[y.size() - 1 - m] = u * pow(alpha_, 1.0 - gamma_);
                    u0 = u;
                    y0 = y[m];
                }
            }
        }

        return result;
    }

    Real ZabrModel::y(const Real strike) const {

        if (close(beta_, 1.0)) {
            return std::log(forward_ / strike) * std::pow(alpha_, gamma_ - 2.0);
        } else {
            return (strike < 0.0 ?
                    std::pow(forward_ , 1.0 - beta_) + std::pow(-strike , 1.0 - beta_) :
                    std::pow(forward_ , 1.0 - beta_) - std::pow(strike, 1.0 - beta_)) *
                       std::pow(alpha_, gamma_ - 2.0) / (1.0 - beta_);
        }

    }

    Real ZabrModel::F(const Real y, const Real u) const {
        Real A = 1.0 + (gamma_ - 2.0) * (gamma_ - 2.0) * nu_ * nu_ * y * y +
                 2.0 * rho_ * (gamma_ - 2.0) * nu_ * y;
        Real B = 2.0 * rho_ * (1.0 - gamma_) * nu_ +
                 2.0 * (1.0 - gamma_) * (gamma_ - 2.0) * nu_ * nu_ * y;
        Real C = (1.0 - gamma_) * (1.0 - gamma_) * nu_ * nu_;
        return (-B * u +
                std::sqrt(B * B * u * u - 4.0 * A * (C * u * u - 1.0))) /
               (2.0 * A);
    }

}