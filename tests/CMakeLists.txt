
# Get all propreties that cmake supports
if(NOT CMAKE_PROPERTY_LIST)
    execute_process(COMMAND cmake --help-property-list OUTPUT_VARIABLE CMAKE_PROPERTY_LIST)
    
    # Convert command output into a CMake list
    string(REGEX REPLACE ";" "\\\\;" CMAKE_PROPERTY_LIST "${CMAKE_PROPERTY_LIST}")
    string(REGEX REPLACE "\n" ";" CMAKE_PROPERTY_LIST "${CMAKE_PROPERTY_LIST}")
    list(REMOVE_DUPLICATES CMAKE_PROPERTY_LIST)
endif()
    
function(print_properties)
    message("CMAKE_PROPERTY_LIST = ${CMAKE_PROPERTY_LIST}")
endfunction()
    
function(print_target_properties target)
    if(NOT TARGET ${target})
      message(STATUS "There is no target named '${target}'")
      return()
    endif()

    foreach(property ${CMAKE_PROPERTY_LIST})
        string(REPLACE "<CONFIG>" "${CMAKE_BUILD_TYPE}" property ${property})

        # Fix https://stackoverflow.com/questions/32197663/how-can-i-remove-the-the-location-property-may-not-be-read-from-target-error-i
        if(property STREQUAL "LOCATION" OR property MATCHES "^LOCATION_" OR property MATCHES "_LOCATION$")
            continue()
        endif()

        get_property(was_set TARGET ${target} PROPERTY ${property} SET)
        if(was_set)
            get_target_property(value ${target} ${property})
            message("${target} ${property} = ${value}")
        endif()
    endforeach()
endfunction()

#############################################
#               Build secureJoin_Tests            #
#############################################

set(SRCS
	UnitTests.cpp
	LowMCPerm_Test.cpp
	ComposedPerm_Test.cpp
	PaillierPerm_Test.cpp
	AdditivePerm_Test.cpp
	"AltModPrf_Test.cpp"
	CorGenerator_Test.cpp
	GMW_Test.cpp
	RadixSort_Test.cpp
	AggTree_Tests.cpp
	AltModPerm_Test.cpp
	CSVParser_Test.cpp
	OmJoin_Test.cpp
	CWrapper_Test.cpp
    Average_Test.cpp
	)

	
add_library(secureJoin_Tests ${SRCS})
if(NOT TARGET WrapperSecJoin)
	message(FATAL_ERROR "WrapperSecJoin not a target")
else()
    #print_target_properties(WrapperSecJoin)
endif()

target_link_libraries(secureJoin_Tests secureJoin WrapperSecJoin)
