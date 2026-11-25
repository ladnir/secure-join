
set(USER_NAME           )      
set(TOKEN               )      
set(GIT_REPOSITORY      "https://github.com/osu-crypto/libOTe.git")
set(GIT_TAG             "2246d8c7063081a95d424cad1decf793a09bcf1d")
set(DEP_NAME            libOTe)          
set(CLONE_DIR "${SECUREJOIN_THIRDPARTY_CLONE_DIR}/${DEP_NAME}")
#set(BUILD_DIR "${CLONE_DIR}/out/build/${SECUREJOIN_CONFIG}")
#set(LOG_FILE  "${CMAKE_CURRENT_LIST_DIR}/log-${DEP_NAME}.txt")


include("${CMAKE_CURRENT_LIST_DIR}/fetch.cmake")

option(LIBOTE_DEV "always build libOTe" OFF)

#if(NOT ${DEP_NAME}_FOUND OR LIBOTE_DEV)
if(APPLE)
    set(LIBOTE_OS_ARGS -DENABLE_RELIC=ON )
else()
    set(LIBOTE_OS_ARGS -DENABLE_SODIUM=ON -DENABLE_MRR_TWIST=ON)
endif()
string (REPLACE ";" "%" CMAKE_PREFIX_PATH_STR "${CMAKE_PREFIX_PATH}")

find_program(GIT git REQUIRED)
set(DOWNLOAD_CMD  ${GIT} clone ${GIT_REPOSITORY})
set(CHECKOUT_CMD  ${GIT} checkout ${GIT_TAG})
    

message("============= Building ${DEP_NAME} =============")
if(NOT EXISTS ${CLONE_DIR})
    run(NAME "Cloning ${GIT_REPOSITORY}" CMD ${DOWNLOAD_CMD} WD ${SECUREJOIN_THIRDPARTY_CLONE_DIR})
endif()


execute_process(COMMAND ${GIT} cat-file -e ${GIT_TAG}^{commit} WORKING_DIRECTORY ${CLONE_DIR}
    RESULT_VARIABLE tag_missing ERROR_QUIET)
if(tag_missing)
    run(NAME "Fetch pinned libOTe" CMD ${GIT} fetch origin ${GIT_TAG} WD ${CLONE_DIR})
endif()
run(NAME "libOTe Checkout ${GIT_TAG} " CMD ${CHECKOUT_CMD}  WD ${CLONE_DIR})
run(NAME "Initialize libOTe submodules" CMD ${GIT} submodule update --init --recursive WD ${CLONE_DIR})

set(ENABLE_SPIN ON)
option(ENABLE_LOGVOLE "Build libOTe LogVOLE" OFF)
option(ENABLE_CIRCUITS " " true)
if(ENABLE_MOCK_OT)
else()
    option(ENABLE_MRR " " true)
endif()

option(ENABLE_IKNP " " true)
option(ENABLE_SOFTSPOKEN_OT " " true)
option(ENABLE_SILENTOT " " true)
option(ENABLE_SILENT_VOLE " " true)
if(APPLE)
    option(ENABLE_RELIC "" true)
else()
    option(ENABLE_SODIUM "" true)
    option(ENABLE_MRR_TWIST "" ENABLE_SODIUM)
endif()
set(ENABLE_SSE ${SECUREJOIN_ENABLE_SSE})
set(OC_THIRDPARTY_CLONE_DIR ${SECUREJOIN_THIRDPARTY_CLONE_DIR})
set(ENABLE_ASAN ${SECUREJOIN_ENABLE_ASAN})
set(ENABLE_BOOST ${SECUREJOIN_ENABLE_BOOST})
set(OC_THIRDPARTY_INSTALL_PREFIX ${SECUREJOIN_THIRDPARTY_DIR})
set(LIBOTE_BUILD_DIR ${SECUREJOIN_BUILD_DIR})


message("add_subdirectory(${CLONE_DIR}/libOTe ${CMAKE_BINARY_DIR}/libOTe)")
add_subdirectory(${CLONE_DIR} ${CMAKE_BINARY_DIR}/libOTe)
message("done with libOTe")
#    run(NAME "libOTe Configure"       CMD ${CONFIGURE_CMD} WD ${CLONE_DIR})
#    run(NAME "libOTe Build"           CMD ${BUILD_CMD}     WD ${CLONE_DIR})
#    run(NAME "libOTe Install"         CMD ${INSTALL_CMD}   WD ${CLONE_DIR})
#
#    message("log ${LOG_FILE}\n==========================================")
#else()
#    message("${DEP_NAME} already fetched.")
#endif()

#install(CODE "
#    if(NOT CMAKE_INSTALL_PREFIX STREQUAL \"${SECUREJOIN_THIRDPARTY_CLONE_DIR}\")
#        execute_process(
#            COMMAND ${SUDO} \${CMAKE_COMMAND} --install ${BUILD_DIR}  --config ${CMAKE_BUILD_TYPE} --prefix \#${CMAKE_INSTALL_PREFIX}
#            WORKING_DIRECTORY ${CLONE_DIR}
#            RESULT_VARIABLE RESULT
#            COMMAND_ECHO STDOUT
#        )
#    endif()
#")
