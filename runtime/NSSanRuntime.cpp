#include "nssan/NSSanRuntime.h"

#include <algorithm>
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

struct RuntimeState {
  std::mutex lock;
  std::unordered_map<std::uintptr_t, double> shadows;
  std::unordered_set<std::string> reported_locations;
  double threshold = 1.0e-5;
  bool halt_on_error = false;
};

RuntimeState &state() {
  static RuntimeState instance;
  static std::once_flag once;
  std::call_once(once, [] {
    RuntimeState &runtime = instance;

    if (const char *raw = std::getenv("NSSAN_THRESHOLD")) {
      char *end = nullptr;
      const double parsed = std::strtod(raw, &end);
      if (end != raw && std::isfinite(parsed) && parsed > 0.0) {
        runtime.threshold = parsed;
      }
    }

    if (const char *halt = std::getenv("NSSAN_HALT_ON_ERROR")) {
      runtime.halt_on_error = std::strcmp(halt, "0") != 0;
    }
  });
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
  RuntimeState &runtime = state();
  const std::string key = locationKey(file, line, column, op_name);

  {
    std::lock_guard<std::mutex> guard(runtime.lock);
    if (!runtime.reported_locations.insert(key).second) {
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

  if (runtime.halt_on_error) {
    std::abort();
  }
}

} // namespace

extern "C" void __nssan_shadow_store(const void *addr, double shadow) {
  if (addr == nullptr) {
    return;
  }

  RuntimeState &runtime = state();
  const auto key = reinterpret_cast<std::uintptr_t>(addr);
  std::lock_guard<std::mutex> guard(runtime.lock);
  runtime.shadows[key] = shadow;
}

extern "C" double __nssan_shadow_load(const void *addr, float current) {
  if (addr == nullptr) {
    return static_cast<double>(current);
  }

  RuntimeState &runtime = state();
  const auto key = reinterpret_cast<std::uintptr_t>(addr);

  std::lock_guard<std::mutex> guard(runtime.lock);
  auto it = runtime.shadows.find(key);
  if (it != runtime.shadows.end()) {
    return it->second;
  }
  return static_cast<double>(current);
}

extern "C" void __nssan_check_float_op(float result,
                                       double shadow,
                                       const char *file,
                                       std::uint32_t line,
                                       std::uint32_t column,
                                       const char *op_name) {
  RuntimeState &runtime = state();

  if (!std::isfinite(result) || !std::isfinite(shadow)) {
    emitReport(file, line, column, op_name, result, shadow, std::numeric_limits<double>::infinity());
    return;
  }

  const double abs_tolerance = absoluteTolerance(runtime.threshold);
  const double diff = std::fabs(static_cast<double>(result) - shadow);
  if (diff <= abs_tolerance) {
    return;
  }

  const double err = relativeError(result, shadow, abs_tolerance);
  if (err > runtime.threshold) {
    emitReport(file, line, column, op_name, result, shadow, err);
  }
}
