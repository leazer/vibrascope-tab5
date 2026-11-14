#include "test_support.hpp"

void run_domain_tests();
void run_probe_codec_tests();
void run_probe_client_tests();
void run_framework_contract_tests();
void run_analysis_selection_tests();
void run_scope_analysis_tests();
void run_keyboard_state_tests();
void run_scope_viewport_tests();
void run_scope_spectrum_tests();
void run_dpi_rotation_tests();
void run_scope_render_cache_tests();
void run_scope_recording_tests();
void run_scope_calibration_tests();

int main()
{
    run_domain_tests();
    run_probe_codec_tests();
    run_probe_client_tests();
    run_framework_contract_tests();
    run_analysis_selection_tests();
    run_scope_analysis_tests();
    run_keyboard_state_tests();
    run_scope_viewport_tests();
    run_scope_spectrum_tests();
    run_dpi_rotation_tests();
    run_scope_render_cache_tests();
    run_scope_recording_tests();
    run_scope_calibration_tests();
    if (test::failures != 0) {
        std::fprintf(stderr, "%d test expectation(s) failed\n", test::failures);
        return 1;
    }
    std::puts("PASS: vhm_host_tests");
    return 0;
}
