cmake_minimum_required(VERSION 3.15)
project(microLime)

set(CMAKE_CXX_STANDARD 14)

add_executable(microLime main.cpp dump.cpp dump.h page.cpp page.h constants.h LimeHeader.h)