


set(SRCS
    "Prf/AltModPrf.cpp"
    "Prf/F2LinearCode.cpp"
    "Prf/F3LinearCode.cpp"
    "Prf/DarkMatter22Prf.cpp"

    
    "CorGenerator/CorGenerator.cpp"
    "CorGenerator/Request.cpp"
    "CorGenerator/Batch.cpp"
    "CorGenerator/OtBatch.cpp"
    "CorGenerator/BinOleBatch.cpp"

    
    "Perm/Permutation.cpp"
    "Perm/ComposedPerm.cpp"
    "Perm/LowMCPerm.cpp"
    "Perm/AdditivePerm.cpp"
    "Perm/AltModPerm.cpp"
    
    "Perm/PaillierPerm.cpp"

    "GMW/Circuit.cpp"
    "GMW/Gmw.cpp"

    "Sort/BitInjection.cpp"
    "Sort/RadixSort.cpp"
    
    "AggTree/AggTree.cpp"
    "AggTree/Level.cpp"
    "AggTree/PlainAggTree.cpp"
    "Aggregate/Average.cpp"
    
    "Util/Trim.cpp"
    "Util/CSVParser.cpp"
    
    "Join/Table.cpp"
    "Join/OmJoin.cpp"
    )

if(SECUREJOIN_ENABLE_PAILLIER)
    set(SRCS ${SRCS})
endif()

add_library(secureJoin STATIC ${SRCS})
if(SECUREJOIN_DEV)
    target_link_libraries(secureJoin libOTe)
else()
    target_link_libraries(secureJoin oc::libOTe)
endif()

configure_file(config.h.in "config.h" )


if(SECUREJOIN_ENABLE_PAILLIER)
    target_link_libraries(secureJoin Paillier)
endif()

if(APPLE)
    target_compile_options(secureJoin PUBLIC
        "-std=c++${SECUREJOIN_CPP_VER}"
    )    
    target_compile_options(secureJoin PRIVATE
        "-Wno-pessimizing-move"
        "-Wno-delete-non-abstract-non-virtual-dtor"
    )
elseif(MSVC)
    message("SECUREJOIN_CPP_VER=${SECUREJOIN_CPP_VER}")
    target_compile_options(secureJoin PUBLIC "/std:c++${SECUREJOIN_CPP_VER}" "/Zc:__cplusplus")

    #set_target_properties(secureJoin
    #    PROPERTIES
    #        CXX_STANDARD ${SECUREJOIN_CPP_VER}
    #        CXX_STANDARD_REQUIRED YES
    #        CXX_EXTENSIONS NO
    #)

    if(SECUREJOIN_ENABLE_ASAN)
        target_compile_options(secureJoin PUBLIC "/fsanitize=address")
        target_link_options(secureJoin PUBLIC "/fsanitize=address")

    endif()
else()
    target_compile_options(secureJoin PUBLIC
        "-std=c++${SECUREJOIN_CPP_VER}"
    )    

    if(SECUREJOIN_ENABLE_ASAN)
        target_compile_options(secureJoin PUBLIC "-fsanitize=address")# "-ftime-report")
        target_link_options(secureJoin PUBLIC "-fsanitize=address")
    endif()
endif()

if(MSVC)
    target_compile_definitions(secureJoin PUBLIC "_ENABLE_EXTENDED_ALIGNED_STORAGE")
else()
    target_compile_options(secureJoin PUBLIC -mavx)
endif()

#target_compile_features(secureJoin PUBLIC cxx_std_17)
# make projects that include secureJoin use this as an include folder
target_include_directories(secureJoin PUBLIC 
                    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
                    $<INSTALL_INTERFACE:include>) 
target_include_directories(secureJoin PUBLIC 
                    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
                    $<INSTALL_INTERFACE:include>) 

