cmake_minimum_required(VERSION 2.8) # Needed for file(COPY ...)

project(MemoryCoin)

enable_testing()

set(MAJOR_VERSION 0)
set(MINOR_VERSION 8)
set(RELEASE_VERSION 99)
set(BUILD_VERSION 0)

set(PACKAGE bitcoin)
set(VERSION ${MAJOR_VERSION}.${MINOR_VERSION}.${RELEASE_VERSION}.${BUILD_VERSION})
set(host ${CMAKE_SYSTEM} ${CMAKE_SYSTEM_PROCESSOR})
set(COPYRIGHT "Copyright (c) 2009-2013 Bitcoin Developers.  All Rights Reserved.")
set(prefix ${CMAKE_INSTALL_PREFIX})

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")
INCLUDE(UsePkgConfig)

# alert.cpp requires pthreads directly?
find_package(Threads REQUIRED)
find_package(BDB REQUIRED)

find_package(Boost 1.45.0 COMPONENTS system filesystem thread program_options
	                             unit_test_framework)
if(NOT ${Boost_FOUND})
    message(FATAL_ERROR "Could not locate Boost.")
endif()
set(Boost_USE_STATIC_LIBS OFF) 
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_RUNTIME OFF)

find_package(OpenSSL)
if(NOT ${OpenSSL_FOUND})
    message(FATAL_ERROR "Could not locate OpenSSL.")
endif()

# memorycoin-qt dependencies (4.4.3 arbitrarily set via docs):
find_package(Qt4 4.4.3 COMPONENTS QtCore QtGui QtNetwork)

# 
# TODO: add cache options for things in configure.ac
#
option(BUILD_QT "Enable QT (default is yes)" ON)

add_subdirectory(src)

find_package(Doxygen)
if(DOXYGEN_FOUND)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/doc/Doxyfile.in 
	           ${CMAKE_CURRENT_BINARY_DIR}/doc/Doxyfile @ONLY)
    add_custom_target(doc ${DOXYGEN_EXECUTABLE}
                      ${CMAKE_CURRENT_BINARY_DIR}/doc/Doxyfile
		      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		      COMMENT "Bitcoin API documentation via Doxygen" VERBATIM)
endif()
