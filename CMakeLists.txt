cmake_minimum_required(VERSION 3.20)

project(
  NSSan
  VERSION 0.1.0
  DESCRIPTION "Numerical Stability Sanitizer for LLVM"
  LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(HandleLLVMOptions OPTIONAL)
include(AddLLVM OPTIONAL)

add_definitions(${LLVM_DEFINITIONS})
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})

if(COMMAND add_llvm_pass_plugin)
  add_llvm_pass_plugin(NSSanPass lib/NSSanPass.cpp)
else()
  add_library(NSSanPass MODULE lib/NSSanPass.cpp)
endif()

target_include_directories(
  NSSanPass
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(MSVC)
  target_compile_options(NSSanPass PRIVATE /EHsc)
else()
  target_compile_options(NSSanPass PRIVATE -fno-rtti)
endif()

if(NOT COMMAND add_llvm_pass_plugin)
  if(TARGET LLVM)
    target_link_libraries(NSSanPass PRIVATE LLVM)
  elseif(COMMAND llvm_map_components_to_libnames)
    llvm_map_components_to_libnames(
      NSSAN_LLVM_LIBS
      Core
      Passes
      Support
      TransformUtils
    )
    target_link_libraries(NSSanPass PRIVATE ${NSSAN_LLVM_LIBS})
  else()
    message(FATAL_ERROR "No suitable LLVM target or component mapping was found.")
  endif()
endif()

if(NOT COMMAND add_llvm_pass_plugin)
  set_target_properties(
    NSSanPass
    PROPERTIES
      PREFIX ""
      OUTPUT_NAME "NSSanPass"
  )
endif()

add_library(NSSanRuntime STATIC runtime/NSSanRuntime.cpp)
target_include_directories(
  NSSanRuntime
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(MSVC)
  target_compile_options(NSSanRuntime PRIVATE /EHsc)
else()
  target_compile_options(NSSanRuntime PRIVATE -fno-sanitize=all)
endif()

add_custom_target(
  nssan-info
  COMMAND ${CMAKE_COMMAND} -E echo "Pass plugin: $<TARGET_FILE:NSSanPass>"
  COMMAND ${CMAKE_COMMAND} -E echo "Runtime lib: $<TARGET_FILE:NSSanRuntime>"
  VERBATIM
)
