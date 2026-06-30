cmake_minimum_required(VERSION 3.16)

project(mcpd3_distributed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

find_package(Threads REQUIRED)

add_library(mcpd3_distributed_protocol
  src/protocol.cpp)
target_include_directories(mcpd3_distributed_protocol PUBLIC
  include
  third_party/mcpd3)

add_library(mcpd3_distributed_runtime
  src/runtime.cpp
  src/tcp.cpp
  third_party/mcpd3/maxflow/graph.cpp
  third_party/mcpd3/maxflow/maxflow.cpp)
target_include_directories(mcpd3_distributed_runtime PUBLIC
  include
  third_party/mcpd3)
target_link_libraries(mcpd3_distributed_runtime PUBLIC
  mcpd3_distributed_protocol
  Threads::Threads)

add_executable(mcpd3_worker
  src/worker_main.cpp)
target_link_libraries(mcpd3_worker PRIVATE
  mcpd3_distributed_runtime)

add_executable(mcpd3_coordinator
  src/coordinator_main.cpp)
target_link_libraries(mcpd3_coordinator PRIVATE
  mcpd3_distributed_runtime)

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
    mcpd3_distributed_protocol)
  add_test(NAME protocol_serialization_test
    COMMAND protocol_serialization_test)

  add_executable(tcp_loopback_test
    tests/tcp_loopback_test.cpp)
  target_link_libraries(tcp_loopback_test PRIVATE
    mcpd3_distributed_runtime)
  add_test(NAME tcp_loopback_test
    COMMAND tcp_loopback_test)
endif ()
