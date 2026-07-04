#pragma once
#include <cmath>
#include <random>

namespace ctream::utilities {
  /*
  template<class T, class F>
  class BinomialGeomSampler{
    bool comp;
    T n;
    F scale;

    BinomialGeomSampler(const T n, const double p) : comp(p > 0.5), n(n), scale(p > 0.5 ? p < 1 ? -1/log(p) : INFINITY : -1.0/log1p(-p)) {}

    template<class RNG> T operator()(RNG& rng) {
      T y{0}, x{0};
      std::exponential_distribution<F> exp_dist(F(1.0));
      while (true) {
        const F v = exp_dist(rng) * scale;
        if (v > static_cast<F>(n)) break;
        y += static_cast<T>(ceil(v));
        if (y >= n) break;
        ++x;
      }
      return comp ? n - x : x;
    }
  };

  template<class T, class F>
  class BinomialTPESampler {
    bool comp;
    T n;
    F r;
    F q;
    F nrq;
    F M;
    F Mi;
    F p1;
    F p2;
    F p3;
    F p4;
    F xM;
    F xL;
    F xR;
    F c;
    F lL;
    F lR;

    BinomialTPESampler(const T n, const double p) : comp(p > 0.5), n(n), r(comp ? 1-p : p), q(comp ? p : 1-p) {
      nrq = n * r * q;
      auto fM = (n+1)*r;
      M = floor(fM);
      Mi = round(M);
      p1 = floor(2.195*sqrt(nrq) - 4.6*q) + 0.5;
      xM = M+0.5;
      xL = xM - p1;
      xR = xM + p1;
      c = 0.134 * 20.5 / (15.3 + M);
      auto a = (fM - xL)/(fM-xL *r);
      lL = a *(1.0 +0.5*a);
      a = (xR - fM)/(xR*q);
      lR = a*(1.0 + 0.5*a);
      p2 = p1 * (1.0 + 2.0*c);
      p3 = p2 + c/lL;
      p4 = p3 + c/lR;
    }

    template<class RNG> T operator()(RNG& rng) {
      T y;
      F u;
      std::uniform_real_distribution<F> unif(0.0, 1.0);
      while (true) {
        u = unif(rng)*p4;
        auto v = unif(rng);
        if (u <= p1) {
          y = static_cast<T>(floor(xM - p1*v + u));
          break;
        }
        if (u <= p2) {
          auto x = xL + (u = p1)/c;
          v = v * c + 1.0 - fabs(M-x+0.5)/p1;
          if (v > 1) continue;
          y = static_cast<T>(floor(x));
        } else if (u <= p3) {
          y = static_cast<T>(floor(xL + log(v)/lL));
          if (y < 0) continue;
          v *= (u - p2) * lL;
        } else {
          y = static_cast<T>(floor(xR - log(v)/lR));
          if (y > n) continue;
          v *= (u - p3) * lR;
        }
        auto k = fabs(y - Mi);
        if (k <= 20 || k>=0.5*nrq-1) {
          auto S = r / q;
          auto a = S*(n+1);
          auto f = 1.0;
          if (Mi < y) for (T i=Mi+1; i<=y; ++i) {
            f *= a/i - S;
          } else if (Mi > y) for (T i=y+1; i<=Mi; ++i) {
            f /= a/i - S;
          }
          if (v > f) continue;
          break;
        } else {
          auto rho = (k / nrq) * ((k*(k/3.0 + 0.625) +1.0/6.0)/nrq + 0.5);
          auto t = -k*k/(2.0*nrq);
          auto a = log(v);
          if (a < t - rho) break;
          if (a > t + rho) continue;
          auto x1 = y+1;
          auto f1 = Mi+1;
          auto z = n+1 - Mi;
          auto w = n-y+1;
          if (a > xM*log(f1/x1) + ((n-Mi)+0.5)*log(z/w) + (y-Mi)*log(w*r/(x1*q)) + lstirling_asym(f1) + lstirling_asym(z) + lstirling_asym(x1) + lstirling_asym(w)) {
            continue;
          }
          break;
        }
      }
      return comp ? n-y : y;
    }
  };

  template<class T, class F>
  class Binomial {
    T n;
    F p;

    Binomial(T n, F p) : n(n), p(p) {}

    template <class RNG> size_t operator()(RNG& rng) {
      F r = p <= 0.5 ? p : 1.0 - p;
      T y;
      if (r * n <= 10.) {
        y = BinomialGeomSampler(n, r)(rng);
      } else {
        y = BinomialTPESampler(n, r)(rng);
      }
      return p <= 0.5 ? y : n - y;
    }

  };
  */

  template<class RNG, class T, class F> T choose(RNG rng, T n, F p) {
    // Compute quantile of Binomial(n, p) distribution at normalized probability nt
    // Based on Julia's choose() from StreamSampling.jl/src/UnweightedSamplingMulti.jl
    // Algorithm: @quantile_fast(8) macro for fast path + linear search fallback
    // Reference: StatsFuns.jl delegates to Rmath.jl for binominvcdf

    // Edge case guards: handle boundary probabilities
    if (p <= 0.0) return 0;      // All mass at k=0
    if (p >= 1.0) return n;      // All mass at k=n

    // z = (1-p)^n
    const F z = std::exp(n * std::log1p(-p));

    // Sample t uniformly from [z, 1.0)
    std::uniform_real_distribution<F> unif(F(z), 1.0);
    const F t = unif(rng);

    // Normalized probability: nt = t/z = t/(1-p)^n
    const F nt = t / z;

    // PHASE 1: Fast quantile approximation using truncated Taylor series (k=8 terms)
    // Computes the CDF of Binomial(n,p) incrementally and stops when CDF > nt
    // Matches Julia's @quantile_fast(8) macro expansion

    // Initial: s = n*p, q = 1-p, x = 1 + s/q (CDF at k=1)
    F s = n * p;
    F q = 1.0 - p;
    F x = 1.0 + s / q;

    if (x > nt) return 1;

    F factorial_i = 1.0;
    // Iterate for k=2 to 8
    for (T i = 2; i <= 8; ++i) {
      // Update s: s *= (n-i)*p
      s *= (static_cast<F>(n) - static_cast<F>(i)) * p;

      // Update q: q *= (1-p)
      q *= (1.0 - p);

      // Compute i! iteratively for efficiency
      factorial_i *= static_cast<F>(i);

      // Add next CDF term: x += s / (q * i!)
      x += s / (q * factorial_i);

      // Return as soon as CDF exceeds threshold
      if (x > nt) return i;
    }

    // PHASE 2: Fallback for quantiles beyond 8-term approximation range
    // Linear search through PMF accumulation (matches StatsFuns.jl approach)
    // Uses incremental PMF computation for numerical stability
    // PMF: P(X=k) = C(n,k) * p^k * (1-p)^(n-k)
    // Incremental: P(X=k) = P(X=k-1) * (n-k+1)*p / (k*(1-p))

    F pmf = std::pow(1.0 - p, static_cast<F>(n));  // P(X=0)
    F cdf = pmf;

    if (cdf > nt) return 0;

    const F one_minus_p = 1.0 - p;
    for (T k = 1; k <= n; ++k) {
      // Update PMF incrementally to avoid overflow/underflow
      // pmf *= (n-k+1)*p / (k*(1-p))
      pmf *= (static_cast<F>(n) - static_cast<F>(k) + 1.0) * p
             / (static_cast<F>(k) * one_minus_p);

      // Accumulate CDF
      cdf += pmf;

      // Return when CDF exceeds normalized probability threshold
      if (cdf > nt) return k;
    }

    // Should not reach here, but return n as safe fallback
    return n;
  }

}