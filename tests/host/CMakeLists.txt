cmake_minimum_required(VERSION 3.20)
project(vhm_tab5_host_tests LANGUAGES CXX)

add_compile_definitions(PLATFORM_BUILD_DESKTOP)

enable_testing()

add_executable(vhm_host_tests
    test_main.cpp
    test_domain.cpp
    test_probe_codec.cpp
    test_probe_client.cpp
    test_framework_contract.cpp
    test_analysis_selection.cpp
    test_scope_analysis.cpp
    test_keyboard_state.cpp
    test_scope_viewport.cpp
    test_scope_spectrum.cpp
    test_dpi_rotation.cpp
    test_scope_render_cache.cpp
    test_scope_recording.cpp
    test_scope_calibration.cpp
    ../../app/apps/app_vibescope/scope_analysis.cpp
    ../../app/apps/app_vibescope/scope_analysis_worker.cpp
    ../../components/probe_protocol/frame_codec.cpp
    ../../components/probe_protocol/messages.cpp
    ../../components/probe_link/probe_client.cpp
)

target_compile_features(vhm_host_tests PRIVATE cxx_std_17)
target_include_directories(vhm_host_tests PRIVATE
    ../../app
    ../../platforms/desktop
    ../../components/vhm_domain/include
    ../../components/probe_protocol/include
    ../../components/probe_link/include
)

if(MSVC)
    target_compile_options(vhm_host_tests PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(vhm_host_tests PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME vhm_host_tests COMMAND vhm_host_tests)
