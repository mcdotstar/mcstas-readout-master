#include <iostream>
#include <map>
#include <random>
#include <ranges>
#include <catch2/catch_test_macros.hpp>

class StandaloneRandom {
  std::mt19937 random_engine{std::default_random_engine{}()};

public:
  void set_random_seed(const uint32_t seed) {
    random_engine.seed(seed);
  }
  int random_poisson(const double mean) {
    std::poisson_distribution<int> distribution(mean);
    return distribution(random_engine);
  }
  int counting_poisson(const double mean) {
    int count{0};
    for (int i=0; i<random_poisson(mean); i++) {count++;}
    return count;
  }
};


TEST_CASE("Poisson asymptotically approaches expected value") {
  auto approaches = [](const double mean, const double tolerance) {
    StandaloneRandom rng;
    rng.set_random_seed(12345);
    const int at_least = std::max(10, static_cast<int>(1 / mean));
    const int most = std::min(1000000, 100000 * at_least);
    double total = 0;
    double average{0};
    int trials{0};
    do {
      for (int i=0; i<at_least; i++) {
        total += rng.random_poisson(mean);
      }
      trials += at_least;
      average = total / trials;
    } while (std::abs(average - mean) >= tolerance && trials < most);
    return std::abs(average - mean) < tolerance;
  };
  for (const double mean : {0.0001, 0.001, 0.01, 0.1, 1.0, 5.0, 10.0, 50.0}) {
    REQUIRE(approaches(mean, mean * 0.01)); // average should be within 1% of mean
  }
}


TEST_CASE("Poisson sampling converges to approximately the expected value") {
  auto approaches = [](const double mean, const double close, const double tolerance) {
    StandaloneRandom rng;
    rng.set_random_seed(12345);
    const int at_least = std::max(100000, static_cast<int>(1 / mean));
    const int most = std::min(100000000, 1000 * at_least);
    double total = 0;
    double average{mean};
    int trials{0};
    double last_average;
    do {
      last_average = average;
      for (int i=0; i<at_least; i++) {
        total += rng.random_poisson(mean);
      }
      trials += at_least;
      average = total / trials;
    } while (std::abs(average - last_average) >= close && trials < most);
    return std::make_tuple(average, std::abs(average - last_average) < close, std::abs(average - mean) < tolerance);
  };
  for (const double mean : {0.001, 0.001, 0.01, 0.1, 1.0, 5.0, 10.0, 50.0}) {
    auto [average, stable, close] = approaches(mean, mean * 0.005, mean * 0.03);
    INFO("Mean: " << mean << ", Average: " << average << ", Stable: " << stable << ", Close: " << close);
    REQUIRE(stable); // average should stabilize
    REQUIRE(close);  // average should be close to mean
  }
}
//
//
// TEST_CASE("Readout uses Poisson event counting wrong") {
//   auto approaches = [](const double mean, const double close) {
//     StandaloneRandom rng;
//     rng.set_random_seed(12345);
//     const int at_least = 1<<20;
//     constexpr int most = 1<<30;
//     double total{0};
//     double counting_total{0};
//     double average{mean};
//     double counting_average{mean};
//     int trials{0};
//     double last_average;
//     double last_counting_average;
//     std::map<int, int> values, counts;
//     do {
//       last_average = average;
//       last_counting_average = counting_average;
//       for (int i=0; i<at_least; i++) {
//         const auto value = rng.random_poisson(mean);
//         total += value;
//         values.insert_or_assign(value, values.contains(value) ? values[value] + 1 : 1);
//         const auto count = rng.counting_poisson(mean);
//         counting_total += count;
//         counts.insert_or_assign(count, counts.contains(count) ? counts[count] + 1 : 1);
//       }
//       trials += at_least;
//       average = total / trials;
//       counting_average = counting_total / trials;
//     } while ((std::abs(average - last_average) >= close || std::abs(counting_average - last_counting_average) >= close) && trials < most);
//     return std::make_tuple(values, counts, trials, most, average, counting_average, std::abs(average - last_average) < close, std::abs(counting_average - last_counting_average) < close);
//   };
//
//   std::vector<std::map<int, int>> values, counts;
//   std::vector means = {0.0001, 0.001, 0.01, 0.1, 1.0, 5.0, 10.0, 50.0};
//
//   for (const double mean : means) {
//     auto [vs, cs, trials, most, average, wrong_average, rnd_converged, sum_converged] = approaches(mean, mean * 0.005);
//     INFO("Trials: " << trials << "/" << most << ", Mean: " << mean << ", Average: " << average << " vs " << wrong_average << ", Rnd Stable: " << rnd_converged << ", Sum Stable: " << sum_converged);
//     REQUIRE(rnd_converged); // average should stabilize
//     REQUIRE(sum_converged);
//     values.push_back(vs);
//     counts.push_back(cs);
//     // if (mean < 0.1) {
//     //   // we have an unintended chance to select an extra count
//     //   REQUIRE(average > mean);
//     // } else if (mean > 1) {
//     //   // we have a chance per iteration that the poisson value is below the mean and we stop early
//     //   REQUIRE(mean > average);
//     // }
//   }
//
//   int max{1};
//   for (const auto & vec: {values, counts}) {
//     for (const auto & mp: vec) {
//       for (const auto &v: mp | std::views::keys) {
//         if (v + 1 > max) max = v + 1;
//       }
//     }
//   }
//   std::stringstream ss;
//   for (const auto & mean: means) {
//     ss << mean;
//     if (mean != means.back()) ss << ",";
//   }
//   ss << std::endl << std::endl;
//
//   for (const auto & vec: {values, counts}) {
//     for (const auto & mp: vec ) {
//       for (int i=0; i<max; ++i) {
//         ss << (mp.contains(i) ? mp.at(i) : 0);
//         if (i+1 < max) ss << ",";
//       }
//       ss << std::endl;
//     }
//     ss << std::endl;
//   }
//
//   std::cout << ss.str() << std::endl;
// }
