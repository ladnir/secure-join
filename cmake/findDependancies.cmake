include(${CMAKE_CURRENT_LIST_DIR}/preamble.cmake)

message(STATUS "SECUREJOIN_THIRDPARTY_DIR=${SECUREJOIN_THIRDPARTY_DIR}")


set(PUSHED_CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH})
set(CMAKE_PREFIX_PATH "${SECUREJOIN_THIRDPARTY_DIR};${CMAKE_PREFIX_PATH}")


#######################################
# libOTe

macro(FIND_LIBOTE)
    set(ARGS ${ARGN})
    set(COMPS)
    
    if(SECUREJOIN_ENABLE_ASAN)
        set(COMPS ${COMPS}  asan)
    else()
        set(COMPS ${COMPS}  no_asan)
    endif()
    if(SECUREJOIN_ENABLE_SSE)
        set(COMPS ${COMPS}  sse)
    else()
        set(COMPS ${COMPS}  no_sse)
    endif()

    if(SECUREJOIN_ENABLE_BOOST)
        set(COMPS ${COMPS}  boost)
    else()
        #set(COMPS ${COMPS}  no_boost)
    endif()



    #explicitly asked to fetch libOTe
    if(FETCH_LIBOTE)
        list(APPEND ARGS NO_DEFAULT_PATH PATHS ${SECUREJOIN_THIRDPARTY_DIR})
    elseif(${NO_CMAKE_SYSTEM_PATH})
        list(APPEND ARGS NO_DEFAULT_PATH PATHS ${CMAKE_PREFIX_PATH})
    endif()
    
    find_package(libOTe ${ARGS} COMPONENTS ${COMPS})

    if(TARGET oc::libOTe)
        set(libOTe_FOUND ON)
    else()
        set(libOTe_FOUND  OFF)
    endif()
endmacro()

if(SECUREJOIN_DEV)
    set(ENABLE_CIRCUITS true)
    set(ENABLE_CIRCUITS true)
    set(ENABLE_MRR true)
    set(ENABLE_IKNP true)
    set(ENABLE_SOFTSPOKEN_OT true)
    set(ENABLE_SILENTOT true)
    set(ENABLE_SILENT_VOLE true)
    if(APPLE)
        set(ENABLE_RELIC true)
    else()
        set(ENABLE_SODIUM true)
        set(ENABLE_MRR_TWIST true)
    endif()
    set(ENABLE_SSE ${SECUREJOIN_ENABLE_SSE})
    set(OC_THIRDPARTY_CLONE_DIR ${SECUREJOIN_THIRDPARTY_CLONE_DIR})
    set(ENABLE_ASAN ${SECUREJOIN_ENABLE_ASAN})
    set(ENABLE_BOOST ${SECUREJOIN_ENABLE_BOOST})
    set(OC_THIRDPARTY_INSTALL_PREFIX ${SECUREJOIN_THIRDPARTY_DIR})
    set(LIBOTE_BUILD_DIR ${SECUREJOIN_BUILD_DIR})

    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../out/libOTe")
        include(${CMAKE_CURRENT_LIST_DIR}/../thirdparty/getLibOTe.cmake)
    endif()

    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../out/libOTe")
else()
    if(FETCH_LIBOTE_AUTO)
        FIND_LIBOTE(QUIET)
        include(${CMAKE_CURRENT_LIST_DIR}/../thirdparty/getLibOTe.cmake)
    endif()
    
    FIND_LIBOTE(REQUIRED)
endif()



# resort the previous prefix path
set(CMAKE_PREFIX_PATH ${PUSHED_CMAKE_PREFIX_PATH})
