cmake_minimum_required(VERSION 3.26)
project(modern_combat_input CXX)

# The Dusklight version to build against: a release tag (e.g. "v1.5.0") or a commit SHA.
if (NOT DUSKLIGHT_VERSION)
    set(DUSKLIGHT_VERSION "95608438c107fc321b82c5878f70a811e7c5913c")
endif ()
# Fetches the Dusklight sources into ./dusklight and provides DUSKLIGHT_DIR.
# Use -DDUSKLIGHT_DIR= to point at an existing Dusklight checkout.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")
add_subdirectory("${DUSKLIGHT_DIR}/sdk" dusklight-sdk EXCLUDE_FROM_ALL)

add_mod(modern_combat_input
        FEATURES game
        SOURCES src/mod.cpp
        MOD_JSON mod.json
)
