#include "nssan/NSSanRuntime.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {

// Shadow storage: lock-free for single-threaded hot path,
// mutex only for report deduplication (cold path).

constexpr std::size_t kShadowSlots = 1u << 16; // 65536 slots
struct ShadowEntry {
  std::atomic<std::uintptr_t> addr{0};
  std::atomic<double> value{0.0};
};

static ShadowEntry g_shadow_table[kShadowSlots];

static std::size_t shadow_index(std::uintptr_t key) {
  // Simple hash — mix bits and mask to table size.
  key ^= key >> 16;
  key *= 0x45d9f3b;
  key ^= key >> 16;
  return key & (kShadowSlots - 1);
}

struct RuntimeConfig {
  double threshold = 1.0e-5;
  bool halt_on_error = false;
};

struct ReportState {
  std::mutex lock;
  std::unordered_set<std::string> reported_locations;
};

RuntimeConfig &config() {
  static RuntimeConfig instance;
  static std::once_flag once;
  std::call_once(once, [] {
    RuntimeConfig &cfg = instance;

    if (const char *raw = std::getenv("NSSAN_THRESHOLD")) {
      char *end = nullptr;
      const double parsed = std::strtod(raw, &end);
      if (end != raw && std::isfinite(parsed) && parsed > 0.0) {
        cfg.threshold = parsed;
      }
    }

    if (const char *halt = std::getenv("NSSAN_HALT_ON_ERROR")) {
      cfg.halt_on_error = std::strcmp(halt, "0") != 0;
    }
  });
  return instance;
}

ReportState &report_state() {
  static ReportState instance;
  return instance;
}

double absoluteTolerance(double threshold) {
  return std::max(threshold * threshold, 1.0e-12);
}

double relativeError(float result, double shadow, double abs_tolerance) {
  const double promoted = static_cast<double>(result);
  const double diff = std::fabs(promoted - shadow);
  const double denom = std::max(std::fabs(shadow), abs_tolerance);
  return diff / denom;
}

std::string locationKey(const char *file,
                        std::uint32_t line,
                        std::uint32_t column,
                        const char *op_name) {
  std::ostringstream out;
  out << (file == nullptr ? "<unknown>" : file) << ':' << line << ':' << column
      << ':' << (op_name == nullptr ? "<unknown>" : op_name);
  return out.str();
}

const char *issueType(float result, double shadow, const char *op_name) {
  if (!std::isfinite(result) || !std::isfinite(shadow)) {
    if (std::isnan(result) || std::isnan(shadow)) {
      return "NaN propagation";
    }
    if (std::isinf(result) || std::isinf(shadow)) {
      return "Infinity propagation";
    }
    return "Non-finite divergence";
  }

  if (op_name != nullptr && std::strcmp(op_name, "fsub") == 0) {
    return "Catastrophic Cancellation";
  }

  return "Numerical Divergence";
}

void emitReport(const char *file,
                std::uint32_t line,
                std::uint32_t column,
                const char *op_name,
                float result,
                double shadow,
                double err) {
  ReportState &rs = report_state();
  const std::string key = locationKey(file, line, column, op_name);

  {
    std::lock_guard<std::mutex> guard(rs.lock);
    if (!rs.reported_locations.insert(key).second) {
      return;
    }
  }

  std::ostringstream message;
  message << "=== NUMERICAL SANITIZER: ERROR ===\n"
          << "  Type:     " << issueType(result, shadow, op_name) << '\n'
          << "  Location: " << (file == nullptr ? "<unknown>" : file) << ':' << line
          << ':' << column << '\n'
          << "  Operation: " << (op_name == nullptr ? "<unknown>" : op_name) << '\n'
          << "  float:    " << std::setprecision(9) << result << '\n'
          << "  shadow:   " << std::setprecision(17) << shadow << '\n';

  if (std::isfinite(err)) {
    message << "  error:    " << std::scientific << err
            << "x threshold exceeded\n";
  } else {
    message << "  error:    non-finite value encountered\n";
  }

  std::cerr << message.str();
  std::cerr.flush();

  if (config().halt_on_error) {
    std::abort();
  }
}

} // namespace

extern "C" void __nssan_shadow_store(const void *addr, double shadow) {
  if (addr == nullptr) {
    return;
  }

  const auto key = reinterpret_cast<std::uintptr_t>(addr);
  const std::size_t idx = shadow_index(key);
  g_shadow_table[idx].addr.store(key, std::memory_order_relaxed);
  g_shadow_table[idx].value.store(shadow, std::memory_order_relaxed);
}

extern "C" double __nssan_shadow_load(const void *addr, float current) {
  if (addr == nullptr) {
    return static_cast<double>(current);
  }

  const auto key = reinterpret_cast<std::uintptr_t>(addr);
  const std::size_t idx = shadow_index(key);
  if (g_shadow_table[idx].addr.load(std::memory_order_relaxed) == key) {
    return g_shadow_table[idx].value.load(std::memory_order_relaxed);
  }
  return static_cast<double>(current);
}

extern "C" void __nssan_check_float_op(float result,
                                       double shadow,
                                       const char *file,
                                       std::uint32_t line,
                                       std::uint32_t column,
                                       const char *op_name) {
  const RuntimeConfig &cfg = config();

  if (!std::isfinite(result) || !std::isfinite(shadow)) {
    emitReport(file, line, column, op_name, result, shadow, std::numeric_limits<double>::infinity());
    return;
  }

  const double abs_tolerance = absoluteTolerance(cfg.threshold);
  const double diff = std::fabs(static_cast<double>(result) - shadow);
  if (diff <= abs_tolerance) {
    return;
  }

  const double err = relativeError(result, shadow, abs_tolerance);
  if (err > cfg.threshold) {
    emitReport(file, line, column, op_name, result, shadow, err);
  }
}
