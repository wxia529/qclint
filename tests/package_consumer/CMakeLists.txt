cmake_minimum_required(VERSION 3.16)
project(qclint_consumer LANGUAGES CXX)

find_package(qclint CONFIG REQUIRED)

add_executable(qclint_consumer main.cpp)
target_link_libraries(qclint_consumer PRIVATE qclint::qclint_core)
