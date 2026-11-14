# Backport I2C RX transaction isolation from ESP-IDF master to the TAB5 SDK.
# https://github.com/espressif/esp-idf/issues/18787
# Keep the installed SDK intact and preserve the archive and object names used by linker.lf.
if(NOT IDF_VERSION_MAJOR EQUAL 5 OR NOT IDF_VERSION_MINOR EQUAL 4 OR NOT IDF_VERSION_PATCH EQUAL 4)
    return()
endif()

idf_component_get_property(i2c_lib esp_driver_i2c COMPONENT_LIB)
idf_component_get_property(i2c_dir esp_driver_i2c COMPONENT_DIR)
set(original "${i2c_dir}/i2c_master.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
file(READ "${original}" driver)

function(tab5_replace_i2c_source old new)
    string(FIND "${driver}" "${old}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "TAB5 I2C RX backport no longer matches the installed ESP-IDF source")
    endif()
    string(REPLACE "${old}" "${new}" result "${driver}")
    set(driver "${result}" PARENT_SCOPE)
endfunction()

# A failed read may never reach the ISR path that clears contains_read.
tab5_replace_i2c_source([=[    i2c_master->read_buf_pos = 0;
]=] [=[    i2c_master->read_buf_pos = 0;
    i2c_master->contains_read = false;
]=])

# Clear pending completion bits before enabling interrupts for a new operation.
tab5_replace_i2c_source([=[    i2c_ll_rxfifo_rst(hal->dev);
    i2c_ll_enable_intr_mask(hal->dev, I2C_LL_MASTER_EVENT_INTR);]=] [=[    i2c_ll_rxfifo_rst(hal->dev);
    i2c_ll_clear_intr_mask(hal->dev, I2C_LL_INTR_MASK);
    i2c_ll_enable_intr_mask(hal->dev, I2C_LL_MASTER_EVENT_INTR);]=])
tab5_replace_i2c_source([=[    i2c_ll_master_set_fractional_divider(hal->dev, 0, 0);
    i2c_ll_enable_intr_mask(hal->dev, I2C_LL_MASTER_EVENT_INTR);]=] [=[    i2c_ll_master_set_fractional_divider(hal->dev, 0, 0);
    bus_handle->contains_read = false;
    i2c_ll_clear_intr_mask(hal->dev, I2C_LL_INTR_MASK);
    i2c_ll_enable_intr_mask(hal->dev, I2C_LL_MASTER_EVENT_INTR);]=])

# Only the READ NACK operation owns a buffer for the last byte.
tab5_replace_i2c_source([=[        if (i2c_master->i2c_trans.ops[i2c_master->read_buf_pos + 1].data) {
            i2c_ll_read_rxfifo(hal->dev, i2c_master->i2c_trans.ops[i2c_master->read_buf_pos + 1].data, 1);
        }]=] [=[        const i2c_operation_t *last = &i2c_master->i2c_trans.ops[i2c_master->read_buf_pos + 1];
        if (last->hw_cmd.op_code == I2C_LL_CMD_READ && last->hw_cmd.ack_val == I2C_NACK_VAL && last->data) {
            i2c_ll_read_rxfifo(hal->dev, last->data, 1);
        }]=])

# The synchronous waiter may run on the other core as soon as it is notified.
set(notify [=[    if (i2c_master->event != I2C_EVENT_ALIVE) {
        xQueueSendFromISR(i2c_master->event_queue, (void *)&i2c_master->event, &HPTaskAwoken);
    }
]=])
tab5_replace_i2c_source("${notify}    if (i2c_master->contains_read" "    if (i2c_master->contains_read")
tab5_replace_i2c_source([=[            i2c_isr_receive_handler(i2c_master);
        }
    }
]=] "            i2c_isr_receive_handler(i2c_master);\n        }\n    }\n${notify}")

set(patched "${CMAKE_CURRENT_BINARY_DIR}/tab5_i2c_backport/i2c_master.c")
set(previous "")
if(EXISTS "${patched}")
    file(READ "${patched}" previous)
endif()
if(NOT driver STREQUAL previous)
    file(WRITE "${patched}" "${driver}")
endif()
get_target_property(i2c_sources ${i2c_lib} SOURCES)
list(FIND i2c_sources "${original}" i2c_source_index)
if(i2c_source_index EQUAL -1)
    message(FATAL_ERROR "TAB5 I2C RX backport could not find the ESP-IDF driver source")
endif()
list(REMOVE_ITEM i2c_sources "${original}")
set_property(TARGET ${i2c_lib} PROPERTY SOURCES "${i2c_sources};${patched}")
target_include_directories(${i2c_lib} PRIVATE "${i2c_dir}")
message(STATUS "TAB5: ESP-IDF 5.4.4 I2C RX backport enabled")
