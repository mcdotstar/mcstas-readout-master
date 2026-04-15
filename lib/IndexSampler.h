#pragma once

#include "ctream/algorithms.hpp"

#include "CollectorStar.h"


/// \brief IndexSampler implements a reservoir sampling-based sampling mechanism for data indices.
///
/// IndexSampler maintains a reservoir sample of the collected item indexes using the WRSWR-SKIP algorithm.
///
class IndexSampler {
public:
    using sampler_t = ctream::ReservoirSamplerWRSWRSKIP<size_t>;

    explicit IndexSampler(const size_t samples, const uint32_t seed = std::random_device{}()): sampler_(samples, seed) {}

    void fit(const size_t index, const double weight) {
        sampler_.fit(index, weight);
        seen_++;
    }

    /// Check if the sampler is still in the filling phase (i.e. has not yet collected enough samples to produce a valid sample)
    /// This can be used to determine if the sampler is still collecting initial samples or if it has enough data to produce a valid sample.
    /// Note that even if the sampler is still filling, it will still produce a sample based on the collected data so far, but it may not be representative until enough samples have been collected.
    [[nodiscard]] int filling() const {
        return sampler_.is_filling();
    }

    std::vector<size_t> value() const {
        if (sampler_.is_filling()) {
            std::stringstream ss;
            ss << "Sampler is still filling - not enough samples collected to produce a valid sample";
            if (seen_ == 0) {
                ss << " (no samples collected yet)";
            }
            throw std::logic_error(ss.str());
        }

        const auto values = sampler_.value();
        return values;
    }

    [[nodiscard]] size_t value(const size_t index) const {
        if (sampler_.is_filling()) {
            std::stringstream ss;
            ss << "Sampler is still filling - not enough samples collected to produce a valid sample";
            if (seen_ == 0) {
                ss << " (no samples collected yet)";
            }
            throw std::logic_error(ss.str());
        }

        return sampler_.value(index);
    }

private:
    sampler_t sampler_;
    size_t seen_{0};
};