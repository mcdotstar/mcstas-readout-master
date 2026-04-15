#pragma once

#include "ctream/algorithms.hpp"

#include "CollectorStar.h"

class DiscreteAfter {
public:
  using sampler_t = ctream::ReservoirSamplerWRSWRSKIP<size_t>;
private:
  CollectorStar collector_;
  std::vector<double> weights_;
  std::optional<sampler_t> sampler_;
  double weight_{0.};

public:

  DiscreteAfter(const size_t data_size, const std::string &name)
      : collector_(data_size, name) {}

  DiscreteAfter(const std::string & description, const std::string &name)
      : collector_(description, name) {}

  DiscreteAfter(const std::string & description, const size_t data_size, const std::string &name)
      : collector_(description, data_size, name) {}

  auto object_size() const {
    return collector_.object_size();
  }

  void fit(const void * data, const double weight) {
    collector_.add(data);
    weights_.push_back(weight);
    weight_ += weight;
  }

  /// Sample a fraction of the collected data. Must be called before value() is called.
  /// Should probably only be called after all data is added -- adding more data after sampling will not affect the sample until sample() is called again.
  /// Fraction scales the number of samples from the total collected weights
  ///
  /// Returns the number of samples that should have been drawn by the sampler.
  size_t sample(const double fraction, const uint32_t seed = std::random_device{}()) {
    if (weights_.empty()) {
      return 0u;
    }
    const auto count = static_cast<size_t>(weight_ * fraction);
    sampler_ = sampler_t(count, seed);
    size_t index{0};
    for (const auto & w: weights_) {
      sampler_->fit(index++, w);
    }
    return count;
  }

  /// Check if the sampler is still in the filling phase (i.e. has not yet collected enough samples to produce a valid sample)
  /// This can be used to determine if the sampler is still collecting initial samples or if it has enough data to produce a valid sample.
  /// Note that even if the sampler is still filling, it will still produce a sample based on the collected data so far, but it may not be representative until enough samples have been collected.
  int filling() const {
    return sampler_->is_filling();
  }

  void oversample() {
    if (!sampler_.has_value()) {
      throw std::logic_error("Must call sample() before oversample()");
    }
    while (filling()) {
      size_t index{0};
      for (const auto & w: weights_) {
        sampler_->fit(index++, w);
      }
    }
  }

  std::vector<uint8_t> value() const {
    if (!sampler_.has_value()) {
      throw std::logic_error("Must call sample() before value()");
    }
    if (filling()) {
      std::stringstream ss;
      ss << "Sampler is still filling - not enough samples collected to produce a valid sample";
      throw std::logic_error(ss.str());
    }
    const auto values = sampler_->value();
    std::vector<uint8_t> result(collector_.object_size() * values.size());
    size_t idx = 0;
    for (const auto i: values) {
      collector_.get(i, result.data() + collector_.object_size() * idx++);
    }
    return result;
  }

  void value(const size_t index, void * data) const {
    if (!sampler_.has_value()) {
      throw std::logic_error("Must call sample() before value()");
    }
    if (filling()) {
      std::stringstream ss;
      ss << "Sampler is still filling - not enough samples collected to produce a valid sample";
      throw std::logic_error(ss.str());
    }
    collector_.get(sampler_->value(index), data);
  }

};