cmake_minimum_required(VERSION "3.16.3")

project("ZipArchive")

add_executable("${PROJECT_NAME}" "ZipArchive.cc")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)