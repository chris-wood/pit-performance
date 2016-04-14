cmake_minimum_required(VERSION 2.8)

project (pit_test)
set(CMAKE_BUILD_TYPE Release)

#set(GCC_COVERAGE_COMPILE_FLAGS "-ftest-coverage -fprofile-arcs -pg")
#set(GCC_COVERAGE_LINK_FLAGS    "-pg")
#set( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS}" )
#set( CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_COVERAGE_LINK_FLAGS}" )

include_directories(
        /Users/cwood/PARC/side/Improvements/src/Libparc
        /Users/cwood/PARC/side/Improvements/src/Libccnx-common
        /Users/cwood/PARC/side/Improvements/src/Athena
        /Users/cwood/PARC/side/Improvements/usr/include/
    )

set(SOURCES pit_test.c)

link_directories(
        /Users/cwood/PARC/side/Improvements/usr/lib
    )

add_executable(pit_test ${SOURCES})
set_target_properties(pit_test PROPERTIES COMPILE_FLAGS "-Wall -pg")

target_link_libraries(pit_test longbow longbow-ansiterm parc ccnx_common ccnx_api_portal ccnx_api_notify ccnx_api_control ccnx_transport_rta athena)

install(TARGETS pit_test DESTINATION bin)
