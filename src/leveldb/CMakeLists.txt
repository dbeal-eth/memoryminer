
file(GLOB HEADERS db/*.h util/*.h table/*.h helpers/memenv/*.h)
file(GLOB SOURCES db/*.cc util/*.cc table/*.cc helpers/memenv/*.cc)

# TODO: Need proper switch between platforms
list(APPEND SOURCES port/port_posix.cc)

# Remove unit tests and main functions from lib:
foreach(SOURCE ${SOURCES})
    if(SOURCE MATCHES "(.*)_test.cc")
        list(REMOVE_ITEM SOURCES ${SOURCE})
    elseif(SOURCE MATCHES "(.*)_main.cc")
        list(REMOVE_ITEM SOURCES ${SOURCE})
    endif()
endforeach()

include_directories(".")
include_directories("include")

# TODO: Need proper switch between platforms 
add_definitions( -DLEVELDB_PLATFORM_POSIX )
	#	LEVELDB_PLATFORM_CHROMIUM
	#       LEVELDB_PLATFORM_WINDOWS

# TODO:
#add_definitions(
	#-DTARGET_OS=OS_WINDOWS_CROSSCOMPILE
	#-DTARGET_OS=Darwin

add_library(leveldb STATIC ${HEADERS} ${SOURCES})

