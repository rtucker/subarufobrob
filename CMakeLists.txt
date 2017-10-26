cmake_minimum_required (VERSION 2.8.11)
	project (fobrob)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

#add_compile_options("-std=c99")
add_definitions(-Wall)
add_definitions(-Wextra)
add_definitions(-Wno-unused-parameter)
add_definitions(-Werror)

# Build our libraries
add_library(encoding STATIC hex.c manchester.c)
add_library(runningavg STATIC runningavg.c)
add_library(demodulator STATIC demodulator.c)
add_library(filter STATIC filter.c)
add_library(protocol STATIC protocol.c)
add_library(modem STATIC demodulator.c filter.c runningavg.c)

# Look for external libraries for RF interfaces
find_library(USB_LIB rtlsdr)
if(NOT USB_LIB)
	message(WARNING "rtlsdr library not found")
else()
	message(STATUS "found rtlsdr library")
	add_executable (fobrob_rtlsdr fobrob.c)
	TARGET_LINK_LIBRARIES(fobrob_rtlsdr encoding modem protocol pthread rtlsdr m)
endif()

find_library(BLADERF_LIB bladeRF)
if(NOT BLADERF_LIB)
	message(WARNING "bladeRF library not found")
else()
	message(STATUS "found bladeRF library")
	add_executable (fobrob_bladeRF fobrob_bladeRF.c)
	TARGET_LINK_LIBRARIES(fobrob_bladeRF encoding modem protocol pthread bladeRF m)
endif()

if(NOT USB_LIB AND NOT BLADERF_LIB)
	message(FATAL_ERROR "no supported RF libraries found!")
endif()

# Support executables
add_executable (rpitxify rpitxify.c)
add_executable (bladeRFify bladeRFify.c)
add_executable (rollthecode rollthecode.c)

TARGET_LINK_LIBRARIES(rpitxify encoding)
TARGET_LINK_LIBRARIES(bladeRFify encoding m)
TARGET_LINK_LIBRARIES(rollthecode encoding protocol)
