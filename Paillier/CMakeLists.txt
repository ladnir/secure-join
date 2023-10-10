
project(Paillier)


#############################################
#           Build Paillier encryption       #
#############################################

file(GLOB_RECURSE SRCS *.cpp)
 

add_library(Paillier ${SRCS})
target_include_directories(Paillier PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/..;${CMAKE_CURRENT_SOURCE_DIR}/../;") 

if(true)
    find_path(GMP_INCLUDE_DIRS gmp.h)
    find_library(GMP_LIBRARIES NAMES gmp)

    if(NOT GMP_INCLUDE_DIRS OR NOT GMP_LIBRARIES)
        message(FATAL_ERROR "Could not find GMP\nGMP_INCLUDE_DIRS=${GMP_INCLUDE_DIRS}\nGMP_LIBRARIES=${GMP_LIBRARIES}")
    endif()
    message(STATUS "GMP_INCLUDE_DIRS = ${GMP_INCLUDE_DIRS}" )
    message(STATUS "GMP_LIBRARIES = ${GMP_LIBRARIES}" )

    add_library(gmp STATIC IMPORTED)
    target_include_directories(gmp INTERFACE ${GMP_INCLUDE_DIRS})
    set_property(TARGET gmp PROPERTY IMPORTED_LOCATION ${GMP_LIBRARIES})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(gmpxx REQUIRED IMPORTED_TARGET gmpxx)
    target_link_libraries(main PkgConfig::gmpxx)

endif()
target_link_libraries(Paillier  oc::cryptoTools gmp)

#target_compile_options(Paillier PUBLIC "-std=c++17")