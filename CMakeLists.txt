cmake_minimum_required(VERSION 3.16)

project(mcpd4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

find_package(Threads REQUIRED)

add_library(mcpd4_protocol
  src/protocol.cpp)
target_include_directories(mcpd4_protocol PUBLIC
  include
  third_party/mcpd3)

add_library(mcpd4_runtime
  src/discovery.cpp
  src/runtime.cpp
  src/tcp.cpp
  third_party/mcpd3/maxflow/graph.cpp
  third_party/mcpd3/maxflow/maxflow.cpp)
target_include_directories(mcpd4_runtime PUBLIC
  include
  third_party/mcpd3)
target_link_libraries(mcpd4_runtime PUBLIC
  mcpd4_protocol
  Threads::Threads)

add_executable(mcpd4_worker
  src/worker_main.cpp)
target_link_libraries(mcpd4_worker PRIVATE
  mcpd4_runtime)

add_executable(mcpd4_discovery
  src/discovery_main.cpp)
target_link_libraries(mcpd4_discovery PRIVATE
  mcpd4_runtime)

add_executable(mcpd4_coordinator
  src/coordinator_main.cpp)
target_link_libraries(mcpd4_coordinator PRIVATE
  mcpd4_runtime)

if (BUILD_TESTING)
  add_executable(distributed_partition_worker_smoke_test
    tests/partition_worker_smoke_test.cpp
    third_party/mcpd3/maxflow/graph.cpp
    third_party/mcpd3/maxflow/maxflow.cpp)
  target_include_directories(distributed_partition_worker_smoke_test PRIVATE
    third_party/mcpd3)
  target_link_libraries(distributed_partition_worker_smoke_test PRIVATE
    Threads::Threads)
  add_test(NAME distributed_partition_worker_smoke_test
    COMMAND distributed_partition_worker_smoke_test)

  add_executable(protocol_serialization_test
    tests/protocol_serialization_test.cpp)
  target_link_libraries(protocol_serialization_test PRIVATE
    mcpd4_protocol)
  add_test(NAME protocol_serialization_test
    COMMAND protocol_serialization_test)

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
    mcpd4_discovery)
  add_test(NAME process_integration_test
    COMMAND process_integration_test
      $<TARGET_FILE:mcpd4_coordinator>
      $<TARGET_FILE:mcpd4_worker>
      $<TARGET_FILE:mcpd4_discovery>
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)
endif ()
