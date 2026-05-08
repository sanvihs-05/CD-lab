#pragma once

#include <cstdint>

extern "C" {

void __nssan_shadow_store(const void *addr, double shadow);
double __nssan_shadow_load(const void *addr, float current);

void __nssan_check_float_op(float result,
                            double shadow,
                            const char *file,
                            std::uint32_t line,
                            std::uint32_t column,
                            const char *op_name);

}

