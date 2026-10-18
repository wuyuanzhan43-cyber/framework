file(READ "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json" manifest)
function(manifest_array field output)
    string(JSON count LENGTH "${manifest}" ${field})
    math(EXPR last "${count} - 1")
    set(items)
    foreach(index RANGE ${last})
        string(JSON item GET "${manifest}" ${field} ${index})
        list(APPEND items "${CMAKE_SOURCE_DIR}/${item}")
    endforeach()
    set(${output} "${items}" PARENT_SCOPE)
endfunction()
manifest_array(firmwareSources sources)
manifest_array(vendorSources vendor_sources)
manifest_array(includes includes)
string(JSON port GET "${manifest}" gccPort)
string(JSON startup GET "${manifest}" gccStartup)
string(JSON runtime GET "${manifest}" gccRuntime)
get_filename_component(port_include "${CMAKE_SOURCE_DIR}/${port}" DIRECTORY)
add_library(fw_vendor STATIC ${vendor_sources} "${CMAKE_SOURCE_DIR}/${port}")
target_include_directories(fw_vendor PUBLIC ${includes} "${port_include}")
target_compile_definitions(fw_vendor PUBLIC STM32F407xx USE_HAL_DRIVER)
add_executable(fw_f407 ${sources} "${CMAKE_SOURCE_DIR}/${startup}" "${CMAKE_SOURCE_DIR}/${runtime}")
option(FW_UART_DMA "Enable USART1 DMA receive/transmit (validated on COM16)" ON)
target_compile_definitions(fw_f407 PRIVATE FW_STM32_UART_DMA=$<BOOL:${FW_UART_DMA}>)
if(FW_UART_DMA)
    message(STATUS "USART1 DMA enabled: validated on COM16; set -DFW_UART_DMA=OFF to use IT")
endif()
target_link_libraries(fw_f407 PRIVATE fw_app fw_vendor)
fw_warnings(fw_f407)
target_link_options(fw_f407 PRIVATE
    "-T${CMAKE_CURRENT_SOURCE_DIR}/stm32f407ve.ld"
    -nostartfiles --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/fw_f407.map" -Wl,--print-memory-usage)
set_target_properties(fw_f407 PROPERTIES SUFFIX ".elf")
add_custom_command(TARGET fw_f407 POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O ihex $<TARGET_FILE_NAME:fw_f407> fw_f407.hex
    COMMAND "${CMAKE_OBJCOPY}" -O binary $<TARGET_FILE_NAME:fw_f407> fw_f407.bin
    COMMAND "${CMAKE_SIZE}" $<TARGET_FILE_NAME:fw_f407>
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:fw_f407>"
    VERBATIM)
message(STATUS "Board profile is explicit: review framework/boards/stm32f407ve/board_config.h before flashing")
