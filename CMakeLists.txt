cmake_minimum_required(VERSION 3.16)

project(mcpd3_distributed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

find_package(Threads REQUIRED)

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
endif ()
