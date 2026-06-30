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
endif ()
