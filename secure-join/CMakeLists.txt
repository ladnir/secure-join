


set(SRCS
    "Prf/mod3.cpp"
    "Prf/ConvertToF3.cpp"
    "Prf/AltModPrf.cpp"
    "Prf/AltModKeyMult.cpp"
    "Prf/AltModPrfProto.cpp"
    "Prf/F2LinearCode.cpp"
    "Prf/F3LinearCode.cpp"

    
    "CorGenerator/CorGenerator.cpp"
    "CorGenerator/Request.cpp"
    "CorGenerator/Batch.cpp"
    "CorGenerator/OtBatch.cpp"
    "CorGenerator/BinOleBatch.cpp"
    "CorGenerator/TritOtBatch.cpp"
    "CorGenerator/F4BitOtBatch.cpp"

    
    "Perm/Permutation.cpp"
    "Perm/PermCorrelation.cpp"
    "Perm/ComposedPerm.cpp"
    "Perm/LowMCPerm.cpp"
    
    "Perm/AltModComposedPerm.cpp"
    "Perm/AltModPerm.cpp"
    
    "Perm/PprfPermGen.cpp"

    "GMW/Circuit.cpp"
    "GMW/Gmw.cpp"

    "Sort/BitInjection.cpp"
    "Sort/RadixSort.cpp"
    "Sort/BatcherMerge.cpp"
    "Sort/PiLogStar.cpp"
    "Sort/PackedLogStar.cpp"
    "Sort/StableSecretExtract.cpp"
    "AggTree/BatchPrefix.cpp"
    
    "AggTree/AggTree.cpp"
    "AggTree/Level.cpp"
    "AggTree/PlainAggTree.cpp"
    "TableOps/GroupBy.cpp"
    "TableOps/Where.cpp"
    "TableOps/WhereParser.cpp"
    "TableOps/Extract.cpp"
    
    "Util/Trim.cpp"
    
    "Join/Table.cpp"
    "Join/OmJoin.cpp"
    "Join/OoJoin.cpp"
    )



add_library(secureJoin STATIC ${SRCS})
target_link_libraries(secureJoin libOTe)

configure_file(config.h.in "config.h" )
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
endif()

#target_compile_features(secureJoin PUBLIC cxx_std_17)
# make projects that include secureJoin use this as an include folder
target_include_directories(secureJoin PUBLIC 
                    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
                    $<INSTALL_INTERFACE:include>) 
target_include_directories(secureJoin PUBLIC 
                    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
                    $<INSTALL_INTERFACE:include>) 

