cmake_minimum_required (VERSION 3.15)

project(secureJoin VERSION 0.1.0)

set(SECUREJOIN_BUILD ON)

# sets some variables
include(cmake/preamble.cmake)

# sets build option
include(cmake/buildOptions.cmake)

# find all the dependancies.
include(cmake/findDependancies.cmake)

# build secureJoin
add_subdirectory(secure-join)

# build the tests.
add_subdirectory(tests)

# link the executable.
add_subdirectory(frontend)

enable_testing()
add_test(NAME pi_logstar_real_crypto COMMAND logstar --self-test)
add_test(NAME asymmetric_merge_real_crypto COMMAND rootmerge --self-test)
add_test(NAME pi_median_real_crypto COMMAND pimedian --self-test)
add_test(NAME shuffled_quicksort_real_crypto COMMAND shuffledquicksort --self-test)

# setup the install
include(cmake/install.cmake)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/secureJoinConfig.cmake"
  INSTALL_DESTINATION lib/cmake/secureJoin
)

install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/secureJoinConfig.cmake"
  DESTINATION lib/cmake/secureJoin
)
