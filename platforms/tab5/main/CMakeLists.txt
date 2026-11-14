file(GLOB_RECURSE APP_LAYER_SRCS
    ../../../app/*.c
    ../../../app/*.cc
    ../../../app/*.cpp
)

set(APP_LAYER_INCS
    ../../../app
)

file(GLOB_RECURSE MY_HAL_SRCS
    ./hal/*.c
    ./hal/*.cc
    ./hal/*.cpp
)

idf_component_register(SRCS "app_main.cpp" ${APP_LAYER_SRCS} ${MY_HAL_SRCS}
                    INCLUDE_DIRS "." ${APP_LAYER_INCS}
                    EMBED_TXTFILES "../audio/canon_in_d.mp3" "../audio/startup_sfx.mp3" "../audio/shutdown_sfx.mp3"
                        "../audio/select.mp3" "../audio/delete.mp3")

# Keep the factory demo's broad component visibility while linking the new
# protocol client explicitly.
target_link_libraries(${COMPONENT_LIB} PRIVATE __idf_probe_link)
# Keep IDF's serial diagnostics and watchdog shutdown. Once the crash page
# is drawn, bypass the final peripheral reset to preserve LCD scanout.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_panic_handler"
    "-Wl,--wrap=esp_system_reset_modules_on_exit"
    "-Wl,--wrap=dw_gdma_channel_register_event_callbacks")

# IDF 5.4.4's framebuffer DMA2D copier is private to esp_lcd. Keep this
# version-specific include private, without changing main's automatic dependencies.
idf_component_get_property(lcd_component_dir esp_lcd COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${lcd_component_dir}/priv_include")

idf_component_get_property(tinyusb_lib espressif__tinyusb COMPONENT_LIB)
idf_component_get_property(tinyusb_dir espressif__tinyusb COMPONENT_DIR)
target_compile_definitions(${tinyusb_lib} PUBLIC CFG_TUSB_CONFIG_FILE="${CMAKE_CURRENT_LIST_DIR}/tab5_tusb_config.h")

# OTG1.1 FIFO size differs across P4 revisions. Keep the allocator inside the
# depth reported by the controller; leave the managed component unmodified.
set(dwc2_dir "${tinyusb_dir}/src/portable/synopsys/dwc2")
set(dwc2_dcd "${dwc2_dir}/dcd_dwc2.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dwc2_dcd}")
file(READ "${dwc2_dcd}" dwc2_source)
set(fifo_assignment "_dcd_data.dfifo_top = dwc2_controller->otg_dfifo_depth;")
string(FIND "${dwc2_source}" "${fifo_assignment}" fifo_offset)
if(fifo_offset EQUAL -1)
    message(FATAL_ERROR "Check the TAB5 OTG1.1 FIFO adjustment against the selected TinyUSB release")
endif()
string(REPLACE "${fifo_assignment}"
    "${fifo_assignment}\n  const dwc2_ghwcfg3_t hwcfg3 = {.value = dwc2->ghwcfg3};\n  if (rhport == 0 && hwcfg3.dfifo_depth && hwcfg3.dfifo_depth < _dcd_data.dfifo_top) {\n    _dcd_data.dfifo_top = hwcfg3.dfifo_depth;\n  }"
    dwc2_source "${dwc2_source}")
set(dwc2_patched "${CMAKE_CURRENT_BINARY_DIR}/tab5_dcd_dwc2.c")
file(CONFIGURE OUTPUT "${dwc2_patched}" CONTENT "${dwc2_source}" @ONLY)
set_source_files_properties("${dwc2_dcd}" TARGET_DIRECTORY ${tinyusb_lib} PROPERTIES HEADER_FILE_ONLY TRUE)
target_include_directories(${tinyusb_lib} PRIVATE "${dwc2_dir}")
target_sources(${tinyusb_lib} PRIVATE "${dwc2_patched}")
