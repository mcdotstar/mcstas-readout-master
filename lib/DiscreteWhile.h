#pragma once

#include "ctream/algorithms.hpp"

#include <optional>
#include <sstream>

#include "RecordBuffer.h"


/// \brief DiscreteWhile implements a reservoir sampling-based collection and sampling mechanism for streaming data.
///
/// DiscreteWhile collects data items of a specified size and name, and maintains a reservoir sample of the collected items using the WRSWR-SKIP algorithm.
/// It allows adding items with associated weights, and retrieving sampled items as byte arrays or by index.
///
/// The current implementation keeps a copy of all collected items in memory, which may not be suitable for very large datasets.
/// Future improvements could include more memory-efficient storage or on-disk buffering.
// TODO create a different variant of the sampler that can work with CollectorStar-style storage without
//      keeping all collected items in memory. This would require modifying the sampler to store arbitrary data
//      as std::vector<uint8_t> blobs instead of typed elements, with a predefined per-element stride.
class DiscreteWhile {
public:
    using sampler_t = ctream::ReservoirSamplerWRSWRSKIP<size_t>;

    DiscreteWhile(const size_t data_size, const std::string &/*name*/, const size_t samples, const uint32_t seed = std::random_device{}())
        : collector_(data_size), sampler_(samples, seed) {}

    DiscreteWhile(const std::string & description, const std::string &/*name*/, const size_t samples, const uint32_t seed = std::random_device{}())
        : collector_(description), sampler_(samples, seed) {}

    DiscreteWhile(const std::string & description, const size_t data_size, const std::string &/*name*/, const size_t samples, const uint32_t seed = std::random_device{}())
        : collector_(description, data_size), sampler_(samples, seed) {}

    auto object_size() const {
        return collector_.object_size();
    }

    void fit(const void * data, const double weight) {
        collector_.add(data);
        sampler_.fit(seen_++, weight);
    }

    /// Check if the sampler is still in the filling phase (i.e. has not yet collected enough samples to produce a valid sample)
    /// This can be used to determine if the sampler is still collecting initial samples or if it has enough data to produce a valid sample.
    /// Note that even if the sampler is still filling, it will still produce a sample based on the collected data so far, but it may not be representative until enough samples have been collected.
    int filling() const {
        return sampler_.is_filling();
    }

    std::vector<uint8_t> value() const {
        if (sampler_.is_filling()) {
            std::stringstream ss;
            ss << "Sampler is still filling - not enough samples collected to produce a valid sample";
            if (seen_ == 0) {
                ss << " (no samples collected yet)";
            }
            throw std::logic_error(ss.str());
        }

        const auto values = sampler_.value();
        std::vector<uint8_t> result(collector_.object_size() * values.size());
        size_t idx = 0;
        for (const auto i: values) {
            collector_.get(i, result.data() + collector_.object_size() * idx++);
        }
        return result;
    }

    void value(const size_t index, void * data) const {
        if (sampler_.is_filling()) {
            std::stringstream ss;
            ss << "Sampler is still filling - not enough samples collected to produce a valid sample";
            if (seen_ == 0) {
                ss << " (no samples collected yet)";
            }
            throw std::logic_error(ss.str());
        }

        collector_.get(sampler_.value(index), data);
    }

private:
    RecordBuffer collector_;
    sampler_t sampler_;
    size_t seen_{0};
};