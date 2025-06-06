cmake_minimum_required(VERSION 3.13)

# Set default build type to Release
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
set(CMAKE_BUILD_TYPE Release)
endif()

set(PICO_BOARD_HEADER_DIRS ${CMAKE_SOURCE_DIR})

# Set platform, board, and compiler
if(NOT PICO_PLATFORM)
	set(PICO_PLATFORM rp2040)
endif()
if(PICO_PLATFORM STREQUAL rp2040)
	set(PICO_BOARD waveshare_rp2040_geek)
	set(PICO_COMPILER pico_arm_cortex_m0plus_gcc)
elseif(PICO_PLATFORM STREQUAL rp2350-arm-s)
	set(PICO_BOARD waveshare_rp2350_geek)
	set(PICO_COMPILER pico_arm_cortex_m33_gcc)
elseif(PICO_PLATFORM STREQUAL rp2350-riscv)
	set(PICO_BOARD waveshare_rp2350_geek)
	set(PICO_COMPILER pico_riscv_gcc)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Pull in SDK (must be before project)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(picosim C CXX ASM)

pico_sdk_init()

set(Z80PACK ${CMAKE_SOURCE_DIR}/../../z80pack)

add_executable(${PROJECT_NAME}
	picosim.c
	dazzler.c
	disks.c
	draw.c
	lcd.c
	lcd_dev.c
	simcfg.c
	simio.c
	simmem.c
	debug.c
	${Z80PACK}/iodevices/rtc80.c
	${Z80PACK}/iodevices/sd-fdc.c
	${Z80PACK}/z80core/sim8080.c
	${Z80PACK}/z80core/simcore.c
	${Z80PACK}/z80core/simdis.c
	${Z80PACK}/z80core/simglb.c
	${Z80PACK}/z80core/simice.c
	${Z80PACK}/z80core/simz80.c
	${Z80PACK}/z80core/simz80-cb.c
	${Z80PACK}/z80core/simz80-dd.c
	${Z80PACK}/z80core/simz80-ddcb.c
	${Z80PACK}/z80core/simz80-ed.c
	${Z80PACK}/z80core/simz80-fd.c
	${Z80PACK}/z80core/simz80-fdcb.c
)

# generate the header file into the source tree
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/uart_tx.pio)

target_include_directories(${PROJECT_NAME} PUBLIC
	${CMAKE_SOURCE_DIR}
	${Z80PACK}/iodevices
	${Z80PACK}/z80core
)

add_subdirectory(fonts)

add_subdirectory(../libs/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src FatFs)
add_subdirectory(../libs/pico-ds3231/lib ds3231)
add_subdirectory(../libs/stdio_msc_usb stdio_msc_usb)

target_compile_definitions(${PROJECT_NAME} PRIVATE
	PICO_USE_FASTEST_SUPPORTED_CLOCK=1
	PICO_STACK_SIZE=4096
	PICO_CORE1_STACK_SIZE=4096
	PICO_HEAP_SIZE=8192
	# frame buffer color depth (12 or 16 bits)
	COLOR_DEPTH=12
	# LCD refresh rate in Hz (60 works well with 12-bit frame buffer)
	LCD_REFRESH=60
	USBD_MANUFACTURER="Z80pack"
)
if(PICO_RP2040)
	target_compile_definitions(${PROJECT_NAME} PRIVATE
		#USBD_PID=0x1056 # Waveshare RP2040-GEEK
		# the official PID assigned to RP2040-GEEK-80 from Raspberry Pi
		# see: https://github.com/raspberrypi/usb-pid
		USBD_PID=0x1095
		USBD_PRODUCT="RP2040-GEEK"
		CONF_FILE="GEEK2040.DAT"
	)
else()
	target_compile_definitions(${PROJECT_NAME} PRIVATE
		USBD_PID=0x10B6 # Waveshare RP2350-GEEK
		USBD_PRODUCT="RP2350-GEEK"
		CONF_FILE="GEEK2350.DAT"
	)
endif()
if(DEBUG80)
	target_compile_definitions(${PROJECT_NAME} PRIVATE
		DEBUG80=1
	)
endif()

# compiler diagnostic options
if(PICO_C_COMPILER_IS_GNU)
	target_compile_options(${PROJECT_NAME} PUBLIC -fdiagnostics-color=always)
elseif(PICO_C_COMPILER_IS_CLANG)
	target_compile_options(${PROJECT_NAME} PUBLIC -fcolor-diagnostics)
endif()
target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Wextra)
if(PICO_C_COMPILER_IS_CLANG)
	target_compile_options(${PROJECT_NAME} PUBLIC -Weverything)
endif()

target_link_libraries(${PROJECT_NAME}
	hardware_adc
	hardware_dma
	hardware_gpio
	hardware_i2c
	hardware_pwm
	hardware_spi
	hardware_sync
	pico_multicore
	pico_stdlib
	tinyusb_device

	fonts

	no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
	pico-ds3231
	stdio_msc_usb
)

pico_add_extra_outputs(${PROJECT_NAME})

target_link_options(${PROJECT_NAME} PRIVATE -Xlinker --print-memory-usage)

pico_set_program_name(${PROJECT_NAME} "z80pack picosim")
if(PICO_RP2040)
	pico_set_program_description(${PROJECT_NAME} "z80pack on Waveshare RP2040-GEEK")
else()
	pico_set_program_description(${PROJECT_NAME} "z80pack on Waveshare RP2350-GEEK")
endif()
pico_set_program_version(${PROJECT_NAME} "1.8")
pico_set_program_url(${PROJECT_NAME} "https://github.com/udo-munk/RP2xxx-GEEK-80")

# disable SDK stdio UART in/out, disable SDK stdio USB in/out
#pico_enable_stdio_uart(${PROJECT_NAME} 1)
#pico_enable_stdio_usb(${PROJECT_NAME} 1)
