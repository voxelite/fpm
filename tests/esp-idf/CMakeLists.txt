# A program for ESP-IDF that uses all of the library: to check that it compiles for that platform.
# It is built by the CI (see .github/workflows/ci.yml), or with `idf.py set-target esp32c3 build` in this directory.
# The options of the library are given like `idf.py -DFPM_DEFINED_OVERFLOW=ON build`.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fpm-esp-idf)
