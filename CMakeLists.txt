cmake_minimum_required(VERSION 3.16)

project(mcpd4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

find_package(Threads REQUIRED)

include(third_party/mcpd3/cmake/McpdCapacity.cmake)
mcpd_configure_capacity(mcpd3_capacity_config
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mcpd3)

option(MCPD4_ENABLE_SNAPPY "Build Snappy transport compression support" ON)
if (MCPD4_ENABLE_SNAPPY)
  if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/snappy/CMakeLists.txt")
    message(FATAL_ERROR
      "MCPD4_ENABLE_SNAPPY=ON requires third_party/snappy. "
      "Run git submodule update --init --recursive or disable it with "
      "-DMCPD4_ENABLE_SNAPPY=OFF.")
  endif ()
  set(SNAPPY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SNAPPY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
  set(SNAPPY_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/snappy EXCLUDE_FROM_ALL)
endif ()

add_library(mcpd4_protocol
  src/delta_codec.cpp
  src/protocol.cpp)
target_include_directories(mcpd4_protocol PUBLIC
  include
  third_party/mcpd3)
target_link_libraries(mcpd4_protocol PUBLIC mcpd3_capacity_config)

add_library(mcpd4_runtime
  src/discovery.cpp
  src/linear_worker.cpp
  src/runtime.cpp
  src/status.cpp
  src/tcp.cpp
  third_party/mcpd3/maxflow/graph.cpp
  third_party/mcpd3/maxflow/maxflow.cpp)
target_include_directories(mcpd4_runtime PUBLIC
  include
  third_party/mcpd3)
target_link_libraries(mcpd4_runtime PUBLIC
  mcpd4_protocol
  Threads::Threads)
if (MCPD4_ENABLE_SNAPPY)
  target_compile_definitions(mcpd4_runtime PUBLIC MCPD4_ENABLE_SNAPPY=1)
  target_link_libraries(mcpd4_runtime PUBLIC snappy)
endif ()

add_executable(mcpd4_worker
  src/worker_main.cpp)
target_link_libraries(mcpd4_worker PRIVATE
  mcpd4_runtime)

add_executable(mcpd4_discovery
  src/discovery_main.cpp)
target_link_libraries(mcpd4_discovery PRIVATE
  mcpd4_runtime)

add_executable(mcpd4_status
  src/status_main.cpp)
target_link_libraries(mcpd4_status PRIVATE
  mcpd4_runtime)

add_executable(mcpd4_coordinator
  src/coordinator_main.cpp)
target_link_libraries(mcpd4_coordinator PRIVATE
  mcpd4_runtime)

add_executable(mcpd4_inprocess_benchmark
  src/inprocess_benchmark_main.cpp)
target_link_libraries(mcpd4_inprocess_benchmark PRIVATE
  mcpd4_runtime)

if (BUILD_TESTING)
  add_executable(distributed_partition_worker_smoke_test
    tests/partition_worker_smoke_test.cpp
    third_party/mcpd3/maxflow/graph.cpp
    third_party/mcpd3/maxflow/maxflow.cpp)
  target_include_directories(distributed_partition_worker_smoke_test PRIVATE
    third_party/mcpd3)
  target_link_libraries(distributed_partition_worker_smoke_test PRIVATE
    mcpd3_capacity_config
    Threads::Threads)
  add_test(NAME distributed_partition_worker_smoke_test
    COMMAND distributed_partition_worker_smoke_test)

  add_executable(protocol_serialization_test
    tests/protocol_serialization_test.cpp)
  target_link_libraries(protocol_serialization_test PRIVATE
    mcpd4_protocol)
  add_test(NAME protocol_serialization_test
    COMMAND protocol_serialization_test)

  add_executable(linear_worker_test
    tests/linear_worker_test.cpp)
  target_link_libraries(linear_worker_test PRIVATE
    mcpd4_runtime)
  add_test(NAME linear_worker_test
    COMMAND linear_worker_test)

  add_executable(tcp_loopback_test
    tests/tcp_loopback_test.cpp)
  target_link_libraries(tcp_loopback_test PRIVATE
    mcpd4_runtime)
  add_test(NAME tcp_loopback_test
    COMMAND tcp_loopback_test)

  add_executable(process_integration_test
    tests/process_integration_test.cpp)
  target_link_libraries(process_integration_test PRIVATE
    mcpd4_runtime)
  add_dependencies(process_integration_test
    mcpd4_coordinator
    mcpd4_worker
    mcpd4_discovery
    mcpd4_status)
  add_test(NAME process_integration_test
    COMMAND process_integration_test
      $<TARGET_FILE:mcpd4_coordinator>
      $<TARGET_FILE:mcpd4_worker>
      $<TARGET_FILE:mcpd4_discovery>
      $<TARGET_FILE:mcpd4_status>
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)
endif ()
