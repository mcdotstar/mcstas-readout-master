#pragma once
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

#include "SenderConfigs.h"

#ifdef WIN32
// Export symbols if compile flags "READOUT_SHARED" and "READOUT_EXPORT" are set on Windows.
    #ifdef READOUT_SHARED
        #ifdef READOUT_EXPORT
            #define RL_API __declspec(dllexport)
        #else
            #define RL_API __declspec(dllimport)
        #endif
    #else
        // Disable definition if linking statically.
        #define RL_API
    #endif
#else
// Disable definition for non-Win32 systems.
#define RL_API
#endif

enum Replay {NONE = 0, ALL = 1, SEQUENTIAL = 2, RANDOM = 4};

/** \brief Receives the per-point instrument parameter values as replay steps through a file
 *
 * Replay calls publish once per (point, parameter) before any of that point's readouts are
 * sent, then point_ready once all of the point's parameters have been published.
 * Implementations forward the values to, e.g., an EPICS mailbox; the transport lives
 * outside this library.
 */
class RL_API ParameterPublisher {
public:
  virtual ~ParameterPublisher() = default;
  virtual void publish(size_t point, const std::string & name, const std::string & value, const std::optional<std::string> & unit) = 0;
  virtual void point_ready(size_t /*point*/) {}
};

class RL_API NullParameterPublisher final : public ParameterPublisher {
public:
  void publish(size_t, const std::string &, const std::string &, const std::optional<std::string> &) override {}
};

/// Prints "point N: name = value [unit]" lines to the provided stream
class RL_API StreamParameterPublisher final : public ParameterPublisher {
  std::ostream & out_;
public:
  explicit StreamParameterPublisher(std::ostream & out): out_{out} {}
  void publish(size_t point, const std::string & name, const std::string & value, const std::optional<std::string> & unit) override;
};

/// Restrict replay to stored readouts with global index first + k * every, for k in [0, number)
struct RL_API ReplaySubset {
  size_t first{0};
  size_t number{0};
  size_t every{1};
};

// New fields need a matching setter in readout_capi.h and a field in the
// mcstas_readout.ReplayConfig Python dataclass (bump READOUT_CAPI_ABI_VERSION on change).
struct RL_API ReplayConfig {
  /// Counting time in seconds. When set, a stored readout with rate-weight w is sent
  /// n ~ Poisson(w * counting_time) times; when unset every stored readout is sent exactly once.
  std::optional<double> counting_time{std::nullopt};
  /// Seed for the sampling and shuffling generator; 0 selects a non-deterministic seed
  uint32_t seed{0};
  /// Shuffle events within each (point, collector group) before sending
  bool random_order{false};
  /// Per-(detector, readout) EFU endpoints; groups without an entry use the default address/port
  SenderConfigs senders{};
  std::string default_address{"127.0.0.1"};
  int default_port{9000};
  /// Number of stored readouts per HDF5 read
  size_t chunk_size{65536};
  std::optional<ReplaySubset> subset{std::nullopt};
  /// Pulse (reference time) repetition rate in Hz; packet pulse times march
  /// forward on this grid, as at a continuously pulsed source (ESS: 14 Hz)
  double pulse_rate{14.0};
  /// Stamp each event at pulse + (tof % period) instead of pulse + tof, so
  /// long-time-of-flight events wrap into the frame they would be detected in
  bool fold_tof{false};
  /// When non-null, replay polls this flag at point and chunk boundaries and
  /// returns early (cleanly) once it is set. Caller-owned; must outlive replay().
  const std::atomic<bool> * stop{nullptr};
};

/** \brief Replay a Collector file to one or more EFUs
 *
 * Steps through the points in the file in order. For each point the instrument parameters
 * are handed to the publisher; once point_ready returns, the sender waits for the next
 * pulse-grid tick (config.pulse_rate) so the point's reference times are wall-clock
 * instants strictly after the parameters were published, then every collector group's
 * readouts for that point are sampled per the config and sent to the group's
 * (detector, readout)-matched EFU endpoint. Within a point, packet pulse times march
 * forward on the pulse grid as the wall clock passes each tick, mimicking a continuously
 * pulsed source.
 *
 * @return true when the file was replayed to completion, false when stopped early via
 *         config.stop. A stop observed between point_ready and the pulse start suppresses
 *         all of that point's events.
 */
RL_API bool replay(const std::string & filename, const ReplayConfig & config, ParameterPublisher & publisher);
RL_API bool replay(const std::string & filename, const ReplayConfig & config);

/** \brief Replay all stored readouts from a file, each exactly once
 *
 * @param filename The name of the HDF5 file containing the events to send
 * @param address The IP address (or FQDN) of the EFU to receive
 * @param port The UDP port at  which the EFU is listening
 * @param control Which readouts to replay and how
 */
RL_API void replay_all(const std::string & filename, const std::string & address, int port, int control);

/** \brief Replay a subset of stored readouts from a file
 *
 * @param filename The name of the HDF5 file containing the events to send
 * @param address The IP address (or FQDN) of the EFU to receive
 * @param port The UDP port at which the EFU is listening
 * @param first The first index to use from the file
 * @param number The number of events to use from the file
 * @param every The number of events (+1) to skip between those pulled from the file
 * @param control Which readouts to replay and how
 */
RL_API void replay_subset(const std::string & filename, const std::string & address, int port, size_t first, size_t number, size_t every, int control);
