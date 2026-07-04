#include <atomic>
#include <exception>
#include <string>
#include <vector>

#include "readout_capi.h"
#include "replay.h"
#include "CollectorClass.h"
#include "version.hpp"

/// Opaque C-API state: the configuration plus the stop flag it points at.
struct readout_replay {
  ReplayConfig config{};
  std::atomic<bool> stop{false};
  readout_replay() { config.stop = &stop; }
};

namespace {

thread_local std::string tl_last_error;

void clear_error() { tl_last_error.clear(); }

int fail(const std::string & message) {
  tl_last_error = message;
  return READOUT_ERROR;
}

int require_handle(const readout_replay_t * handle) {
  if (handle == nullptr) {
    return fail("readout_replay handle is NULL");
  }
  return READOUT_OK;
}

int require_string(const char * value, const char * what) {
  if (value == nullptr) {
    return fail(std::string(what) + " is NULL");
  }
  return READOUT_OK;
}

std::vector<std::string> to_strings(const char * const * names, const size_t n) {
  std::vector<std::string> out;
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    if (names[i] == nullptr) {
      throw std::runtime_error("input filename " + std::to_string(i) + " is NULL");
    }
    out.emplace_back(names[i]);
  }
  return out;
}

/// Non-std tag thrown by CApiPublisher when a callback requests a stop, so the
/// abort happens immediately (before the point's pulse starts) and is
/// distinguishable from real errors at the readout_replay_run boundary.
struct replay_cancelled {};

/// Adapts the C callback pair to the ParameterPublisher interface.
class CApiPublisher final : public ParameterPublisher {
  readout_publish_cb publish_;
  readout_point_ready_cb point_ready_;
  void * user_data_;
public:
  CApiPublisher(const readout_publish_cb publish, const readout_point_ready_cb point_ready, void * user_data)
    : publish_{publish}, point_ready_{point_ready}, user_data_{user_data} {}
  void publish(const size_t point, const std::string & name, const std::string & value,
               const std::optional<std::string> & unit) override {
    if (publish_ == nullptr) {
      return;
    }
    if (publish_(user_data_, point, name.c_str(), value.c_str(), unit.has_value() ? unit->c_str() : nullptr)) {
      throw replay_cancelled{};
    }
  }
  void point_ready(const size_t point) override {
    if (point_ready_ != nullptr && point_ready_(user_data_, point)) {
      throw replay_cancelled{};
    }
  }
};

/// Run f inside the standard boundary: no exception escapes, errors are recorded.
template<class F>
int guarded(F && f) {
  clear_error();
  try {
    return f();
  } catch (const std::exception & e) {
    return fail(e.what());
  } catch (...) {
    return fail("unknown error");
  }
}

template<class F>
int guarded_combine(const char * out_filename, const char * const * in_filenames, const size_t n_in,
                    const char * what, F && f) {
  return guarded([&]() -> int {
    if (require_string(out_filename, "output filename")) return READOUT_ERROR;
    if (in_filenames == nullptr) return fail("input filename array is NULL");
    const auto inputs = to_strings(in_filenames, n_in);
    if (!f(out_filename, inputs)) {
      return fail(std::string(what) + " failed; diagnostic detail on stderr");
    }
    return READOUT_OK;
  });
}

} // namespace

extern "C" {

int readout_capi_abi_version(void) { return READOUT_CAPI_ABI_VERSION; }

const char * readout_version(void) { return libreadout::version::version_number; }

const char * readout_last_error(void) { return tl_last_error.c_str(); }

readout_replay_t * readout_replay_create(void) {
  clear_error();
  try {
    return new readout_replay();
  } catch (...) {
    tl_last_error = "failed to allocate readout_replay handle";
    return nullptr;
  }
}

void readout_replay_destroy(readout_replay_t * handle) {
  delete handle;
}

int readout_replay_set_counting_time(readout_replay_t * handle, const double seconds) {
  return guarded([&]() -> int {
    if (require_handle(handle)) return READOUT_ERROR;
    if (!(seconds > 0.)) return fail("counting time must be positive");
    handle->config.counting_time = seconds;
    return READOUT_OK;
  });
}

int readout_replay_clear_counting_time(readout_replay_t * handle) {
  return guarded([&]() -> int {
    if (require_handle(handle)) return READOUT_ERROR;
    handle->config.counting_time = std::nullopt;
    return READOUT_OK;
  });
}

int readout_replay_set_seed(readout_replay_t * handle, const uint32_t seed) {
  return guarded([&]() -> int {
    if (require_handle(handle)) return READOUT_ERROR;
    handle->config.seed = seed;
    return READOUT_OK;
  });
}

int readout_replay_set_random_order(readout_replay_t * handle, const int enable) {
  return guarded([&]() -> int {
    if (require_handle(handle)) return READOUT_ERROR;
    handle->config.random_order = enable != 0;
    return READOUT_OK;
  });
}

int readout_replay_set_default_endpoint(readout_replay_t * handle, const char * address, const int port) {
  return guarded([&]() -> int {
    if (require_handle(handle)) return READOUT_ERROR;
    if (require_string(address, "address")) return READOUT_ERROR;
    if (port < 1 || port > 65535) return fail("port must be in [1, 65535]");
    handle->config.default_address = address;
    handle->config.default_port = port;
    return READOUT_OK;
  });
}

int readout_replay_set_chunk_size(readout_replay_t * handle, const uint64_t chunk) {
  return guarded([&]() -> int {
    if (require_handle(handle)) return READOUT_ERROR;
    if (chunk == 0) return fail("chunk size must be at least 1");
    handle->config.chunk_size = chunk;
    return READOUT_OK;
  });
}

int readout_replay_set_subset(readout_replay_t * handle, const uint64_t first, const uint64_t number, const uint64_t every) {
  return guarded([&]() -> int {
    if (require_handle(handle)) return READOUT_ERROR;
    if (every == 0) return fail("subset step (every) must be at least 1");
    handle->config.subset = ReplaySubset{first, number, every};
    return READOUT_OK;
  });
}

int readout_replay_clear_subset(readout_replay_t * handle) {
  return guarded([&]() -> int {
    if (require_handle(handle)) return READOUT_ERROR;
    handle->config.subset = std::nullopt;
    return READOUT_OK;
  });
}

int readout_replay_set_pulse_rate(readout_replay_t * handle, const double hz) {
  return guarded([&]() -> int {
    if (require_handle(handle)) return READOUT_ERROR;
    if (!(hz > 0.)) return fail("pulse rate must be positive");
    handle->config.pulse_rate = hz;
    return READOUT_OK;
  });
}

int readout_replay_set_fold_tof(readout_replay_t * handle, const int enable) {
  return guarded([&]() -> int {
    if (require_handle(handle)) return READOUT_ERROR;
    handle->config.fold_tof = enable != 0;
    return READOUT_OK;
  });
}

int readout_replay_set_senders_json(readout_replay_t * handle, const char * json_text) {
  return guarded([&]() -> int {
    if (require_handle(handle)) return READOUT_ERROR;
    if (require_string(json_text, "senders JSON")) return READOUT_ERROR;
    handle->config.senders = SenderConfigs::from_json(json_text);
    return READOUT_OK;
  });
}

int readout_replay_run(readout_replay_t * handle, const char * filename,
                       const readout_publish_cb publish,
                       const readout_point_ready_cb point_ready,
                       void * user_data) {
  clear_error();
  if (require_handle(handle)) return READOUT_ERROR;
  if (require_string(filename, "filename")) return READOUT_ERROR;
  try {
    CApiPublisher publisher(publish, point_ready, user_data);
    return replay(filename, handle->config, publisher) ? READOUT_OK : READOUT_STOPPED;
  } catch (const replay_cancelled &) {
    return READOUT_STOPPED;
  } catch (const std::exception & e) {
    return fail(e.what());
  } catch (...) {
    return fail("unknown error during replay");
  }
}

void readout_replay_request_stop(readout_replay_t * handle) {
  if (handle != nullptr) {
    handle->stop.store(true, std::memory_order_relaxed);
  }
}

int readout_replay_stop_requested(const readout_replay_t * handle) {
  return handle != nullptr && handle->stop.load(std::memory_order_relaxed) ? 1 : 0;
}

void readout_replay_reset_stop(readout_replay_t * handle) {
  if (handle != nullptr) {
    handle->stop.store(false, std::memory_order_relaxed);
  }
}

int64_t readout_validate_collector_file(const char * filename) {
  clear_error();
  if (require_string(filename, "filename")) return READOUT_ERROR;
  try {
    const int points = validate_collector_file(filename);
    if (points < 0) {
      return fail(std::string(filename) + " is not a valid collector file");
    }
    return points;
  } catch (const std::exception & e) {
    return fail(e.what());
  } catch (...) {
    return fail("unknown error validating collector file");
  }
}

int readout_append_collector_files(const char * out_filename,
                                   const char * const * in_filenames, const size_t n_in,
                                   const int reset_datasets) {
  return guarded_combine(out_filename, in_filenames, n_in, "append_collector_files",
    [reset_datasets](const std::string & out, const std::vector<std::string> & in) {
      return append_collector_files(out, in, reset_datasets != 0);
    });
}

int readout_concatenate_collector_files(const char * out_filename,
                                        const char * const * in_filenames, const size_t n_in) {
  return guarded_combine(out_filename, in_filenames, n_in, "concatenate_collector_files",
    [](const std::string & out, const std::vector<std::string> & in) {
      return concatenate_collector_files(out, in);
    });
}

int readout_combine_collector_files(const char * out_filename,
                                    const char * const * in_filenames, const size_t n_in) {
  return guarded_combine(out_filename, in_filenames, n_in, "combine_collector_files",
    [](const std::string & out, const std::vector<std::string> & in) {
      return combine_collector_files(out, in);
    });
}

} // extern "C"
