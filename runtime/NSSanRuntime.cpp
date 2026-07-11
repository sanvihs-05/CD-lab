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

// --- Run summary counters -------------------------------------------------
// Relaxed atomics updated on the hot path; the totals are printed once at
// program exit by printRunSummary() (registered via std::atexit).
std::atomic<std::uint64_t> g_checks_total{0};
std::atomic<std::uint64_t> g_issue_cancel{0};
std::atomic<std::uint64_t> g_issue_divergence{0};
std::atomic<std::uint64_t> g_issue_nan{0};
std::atomic<std::uint64_t> g_issue_inf{0};

void printRunSummary() {
  const std::uint64_t checks = g_checks_total.load(std::memory_order_relaxed);
  const std::uint64_t cancel = g_issue_cancel.load(std::memory_order_relaxed);
  const std::uint64_t divergence = g_issue_divergence.load(std::memory_order_relaxed);
  const std::uint64_t nan_count = g_issue_nan.load(std::memory_order_relaxed);
  const std::uint64_t inf_count = g_issue_inf.load(std::memory_order_relaxed);
  const std::uint64_t total = cancel + divergence + nan_count + inf_count;

  std::ostringstream out;
  out << "=== NSSan SUMMARY ===\n"
      << "  Float operations checked: " << checks << '\n'
      << "  Numerical issues found:   " << total << " unique site(s)\n";
  if (total > 0) {
    if (cancel > 0)     out << "    Catastrophic Cancellation: " << cancel << '\n';
    if (divergence > 0) out << "    Numerical Divergence:      " << divergence << '\n';
    if (nan_count > 0)  out << "    NaN propagation:           " << nan_count << '\n';
    if (inf_count > 0)  out << "    Infinity propagation:      " << inf_count << '\n';
    out << "  Result: ISSUES DETECTED\n";
  } else {
    out << "  Result: CLEAN (no numerical issues detected)\n";
  }

  std::cerr << out.str();
  std::cerr.flush();
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

    // Print a one-line verdict + issue breakdown when the program exits.
    std::atexit(printRunSummary);
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

// Estimate how many of float's ~7.2 significant decimal digits (24-bit
// mantissa) were destroyed, given a relative error. A relative error of `err`
// leaves about -log10(err) correct digits; the rest are lost.
double significantDigitsLost(double err) {
  if (!(err > 0.0) || !std::isfinite(err)) {
    return 0.0;
  }
  const double correct_digits = -std::log10(err);
  double lost = 7.2 - correct_digits;
  if (lost < 0.0) lost = 0.0;
  if (lost > 7.2) lost = 7.2;
  return lost;
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

  const char *type = issueType(result, shadow, op_name);
  if (std::strcmp(type, "Catastrophic Cancellation") == 0) {
    g_issue_cancel.fetch_add(1, std::memory_order_relaxed);
  } else if (std::strcmp(type, "NaN propagation") == 0) {
    g_issue_nan.fetch_add(1, std::memory_order_relaxed);
  } else if (std::strcmp(type, "Infinity propagation") == 0) {
    g_issue_inf.fetch_add(1, std::memory_order_relaxed);
  } else {
    g_issue_divergence.fetch_add(1, std::memory_order_relaxed);
  }

  std::ostringstream message;
  message << "=== NUMERICAL SANITIZER: ERROR ===\n"
          << "  Type:     " << type << '\n'
          << "  Location: " << (file == nullptr ? "<unknown>" : file) << ':' << line
          << ':' << column << '\n'
          << "  Operation: " << (op_name == nullptr ? "<unknown>" : op_name) << '\n'
          << "  float:    " << std::setprecision(9) << result << '\n'
          << "  shadow:   " << std::setprecision(17) << shadow << '\n';

  if (std::isfinite(err)) {
    message << "  error:    " << std::scientific << err
            << "x threshold exceeded\n"
            << "  precision: ~" << std::fixed << std::setprecision(1)
            << significantDigitsLost(err) << " of 7.2 significant digits lost\n";
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
  g_checks_total.fetch_add(1, std::memory_order_relaxed);

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
