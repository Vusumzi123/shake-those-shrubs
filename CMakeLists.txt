cmake_minimum_required(VERSION 3.21)

project(ShakeThoseShrubs VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CommonLibSSE-NG defaults these ON; disable for a lean, faster test build.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(COMMONLIB_ENABLE_IPO OFF CACHE BOOL "" FORCE)

# CommonLibSSE-NG as a source dependency (Linux cross-compile target).
include(FetchContent)
FetchContent_Declare(
	commonlibsse-ng
	GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
	GIT_TAG v7.5.1
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(commonlibsse-ng)

# CommonLibSSE-NG's add_commonlibsse_plugin() helper (SKSE_Templates convention).
include("${commonlibsse-ng_SOURCE_DIR}/cmake/CommonLibSSE.cmake")

add_commonlibsse_plugin(
	ShakeThoseShrubs
	NAME "ShakeThoseShrubs"
	AUTHOR "vuszi"
	VERSION ${PROJECT_VERSION}
	SOURCES src/plugin.cpp
)

target_compile_features(ShakeThoseShrubs PRIVATE cxx_std_23)
target_precompile_headers(ShakeThoseShrubs PRIVATE PCH.h)

set_target_properties(
	ShakeThoseShrubs
	PROPERTIES
	PREFIX ""
	SUFFIX ".dll"
)
