//
// Created by g on 26/10/22.
//

#ifndef MCSTAS_UDP_TRANSMIT_EFU_TIME_H
#define MCSTAS_UDP_TRANSMIT_EFU_TIME_H
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

/** \brief ESS EFU two-integer timestamp: whole seconds since the UNIX epoch
 * plus ticks of the 88.052499 MHz readout clock within the second.
 *
 * Arithmetic keeps the tick field normalized (always less than one second's
 * worth of ticks), matching how the EFU interprets the high/low pair.
 */
class efu_time {
protected:
  uint32_t _h;
  uint32_t _l;
public:
  static constexpr uint64_t ticks = 88052499;

  /** \brief A time equivalent to that used in the ESS Event-Formation-Unit
   *
   * \param high seconds since the UNIX epoch, 1970-01-01 00:00
   * \param low number of clock ticks in a 88.052499 MHz oscillator
   * */
  efu_time(const uint32_t high, const uint32_t low): _h(high), _l(low) {
    if (_l > ticks) {
      _h += _l / ticks;
      _l %= ticks;
    }
  };

  explicit efu_time(const std::pair<uint32_t, uint32_t> & p): _h(p.first), _l(p.second) {}

  /**
   * \brief Conversion from floating point time representation
   * \param time seconds since the UNIX epoch
   * */
  explicit efu_time(const double time)
  : _h(static_cast<uint32_t>(time)), _l(static_cast<uint32_t>(std::fmod(time, 1.0) * ticks)) {}

  /**
   * \brief Use the system clock to construct an EFU consistent time stamp
   */
  efu_time(): _h(0), _l(0) {
    using std::chrono::duration_cast, std::chrono::nanoseconds, std::chrono::system_clock;
    constexpr uint64_t nps = 1000000000u; // nanoseconds per second
    const auto ns = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
    const auto seconds = ns / nps;
    const auto tick_count = ((ns % nps) * ticks) / nps;
    _h = static_cast<uint32_t>(seconds);
    _l = static_cast<uint32_t>(tick_count);
  }

  uint32_t high() const {return _h;}
  uint32_t low() const {return _l;}

  bool operator==(const efu_time& other) const {
    return (_h == other._h) && (_l == other._l);
  }
  bool operator<(const efu_time& other) const {
    if (_h < other._h) return true;
    if (_h > other._h) return false;
    return _l < other._l;
  }
  bool operator>(const efu_time& other) const {
    if (_h > other._h) return true;
    if (_h < other._h) return false;
    return _l > other._l;
  }

  bool operator>=(const efu_time & other) const {
    return !(*this < other);
  }

  bool operator>=(const efu_time * other) const {
    return *this >= *other;
  }

  efu_time operator+(const efu_time & other) const {
    // the constructor ensures l is always less than ticks
    // and 2*ticks is _well_ within the dynamic range of uint32_t
    return {_h + other._h, _l + other._l};
  }

  efu_time operator+(const efu_time * other) const {
    return *this + *other;
  }

  efu_time operator-(const efu_time & other) const {
    if (*this < other) {
      std::cout << *this << " - " << other << std::endl;
      throw std::runtime_error("Subtracting a larger time?");
    }
    uint32_t h = _h - other._h;
    if (_l >= other._l) return {h, _l - other._l};
    auto r = ticks + static_cast<uint64_t>(_l) - static_cast<uint64_t>(other._l);
    return {h - 1u, static_cast<uint32_t>(r)};
  }

  efu_time operator-(const efu_time * other) const {
    return *this - *other;
  }

  uint64_t total_ticks() const {
    return static_cast<uint64_t>(_h) * ticks + static_cast<uint64_t>(_l);
  }

  uint64_t total_nanoseconds() const {
    constexpr uint64_t nps = 1000000000u;
    return static_cast<uint64_t>(_h) * nps + (static_cast<uint64_t>(_l) * nps) / ticks;
  }

  /// Nanoseconds since the UNIX epoch, to the nearest tick
  static efu_time from_nanoseconds(const uint64_t ns) {
    constexpr uint64_t nps = 1000000000u;
    auto high = ns / nps;
    auto low = ((ns % nps) * ticks + nps / 2) / nps;
    if (low >= ticks) {
      ++high;
      low -= ticks;
    }
    return {static_cast<uint32_t>(high), static_cast<uint32_t>(low)};
  }

  /// The system clock, in nanoseconds since the UNIX epoch
  static uint64_t now_nanoseconds() {
    using std::chrono::duration_cast, std::chrono::nanoseconds, std::chrono::system_clock;
    return static_cast<uint64_t>(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
  }

  uint32_t operator/(const efu_time & other) const {
    return static_cast<uint32_t>(total_ticks() / other.total_ticks());
  }

  efu_time operator*(const uint32_t m) const {
    auto h = _h * m;
    auto r = static_cast<uint64_t>(_l) * static_cast<uint64_t>(m);
    if (r > ticks){
      h += static_cast<uint32_t>(r / ticks);
      r = r % ticks;
    }
    return {h, static_cast<uint32_t>(r)};
  }

  efu_time operator%(const efu_time& m) const {
    return *this - m * (*this / m);
  }

  efu_time operator%(const efu_time * m) const {
    return *this % *m;
  }

  friend std::ostream& operator<<(std::ostream& os, const efu_time& dt) {
    os << "(" << dt.high() << "," << dt.low() << ")";
    return os;
  }
  friend std::ostream & operator<<(std::ostream& os, const efu_time* dt) {
    os << "(" << dt->high() << "," << dt->low() << ")";
    return os;
  }
};

/** \brief The source's pulses: pulse k is at k periods after the UNIX epoch.
 *
 * Anchored on the epoch rather than on whenever a Readout or Sender happens to be
 * created, so every one of them -- every MPI rank of a simulation, every replay --
 * and anything else keeping the same grid, like mccode-plumber's mp-tdc, agree on
 * when a pulse was. The period is a whole number of nanoseconds, round(1e9 / rate),
 * which is also what the EFU takes an ESS pulse to be.
 */
class pulse_grid {
  uint64_t period_ns_;
public:
  explicit pulse_grid(const uint64_t period_ns): period_ns_(period_ns) {
    if (period_ns_ == 0) throw std::invalid_argument("pulse_grid period must be positive");
  }
  static pulse_grid from_rate(const double rate) {
    if (!(rate > 0)) throw std::invalid_argument("pulse_grid rate must be positive");
    return pulse_grid(static_cast<uint64_t>(std::llround(1e9 / rate)));
  }
  /// For a period given as a time: to the nearest nanosecond. efu_time(double)
  /// truncates to a whole tick, so efu_time(1.0 / 14) is 2 ns short of an ESS
  /// pulse; prefer from_rate.
  static pulse_grid from_period(const efu_time & period) {
    const auto ns = static_cast<double>(period.total_ticks()) * 1e9 / static_cast<double>(efu_time::ticks);
    return pulse_grid(static_cast<uint64_t>(std::llround(ns)));
  }
  [[nodiscard]] uint64_t period_ns() const { return period_ns_; }
  [[nodiscard]] efu_time period() const { return efu_time::from_nanoseconds(period_ns_); }
  /// The latest pulse at or before ns
  [[nodiscard]] uint64_t at_or_before(const uint64_t ns) const { return ns / period_ns_ * period_ns_; }
  /// The first pulse strictly after ns
  [[nodiscard]] uint64_t after(const uint64_t ns) const { return (ns / period_ns_ + 1) * period_ns_; }
};

#endif // MCSTAS_UDP_TRANSMIT_EFU_TIME_H
