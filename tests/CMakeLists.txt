# ── Unit tests ─────────────────────────────────────────────────────────────────
#
# Built only when Qt6::Test is available (some minimal Qt installations may
# not ship it — see the top-level CMakeLists.txt find_package(Qt6 QUIET
# COMPONENTS Test) call). Each test is a small, headless (QTEST_GUILESS_MAIN)
# executable linked against solakon-core, so no live Modbus/TCP connection or
# display server is required.

if(NOT TARGET Qt6::Test)
    message(STATUS "Qt6::Test not found — skipping unit tests (build with qt6-test-devel / qt6-base-dev to enable)")
    return()
endif()

function(add_solakon_test test_name)
    add_executable(${test_name} ${ARGN})
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${test_name} PRIVATE
        solakon-core
        Qt6::Core
        Qt6::SerialBus
        Qt6::Test
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_solakon_test(test_registerparser test_registerparser.cpp)
add_solakon_test(test_workmode        test_workmode.cpp)
add_solakon_test(test_mockmodbusapi   test_mockmodbusapi.cpp mockmodbusapi.h)
