cmake_minimum_required(VERSION "3.16.3")

project("ZipArchive")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable("${PROJECT_NAME}" "ZipArchive.cc")