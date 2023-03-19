cmake_minimum_required(VERSION 3.2)

project(pzpipe)

set(SRCDIR ${CMAKE_CURRENT_SOURCE_DIR})
set(OBJDIR ${CMAKE_CURRENT_BINARY_DIR})

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
  message(STATUS "Build type: Release")
else()
  if (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(WARNING "CMake build type is set to ${CMAKE_BUILD_TYPE}! This might result in bad performance!")
  else()
    message(STATUS "Build type: Release")
  endif()
endif()

set(CMAKE_CXX_STANDARD 20)
if (UNIX)
  set(CMAKE_C_STANDARD 99)
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
endif()

include_directories(AFTER ${SRCDIR})

function(add_stem2file VAR pattern stem)
    set(TMPV ${${VAR}})
    foreach(IND ${stem})
        string(REGEX REPLACE "^[ ]*([0-9a-zA-Z_%]+)[ ]*$" \\1 IND ${IND})
        string(REPLACE "%STEM%" ${IND} NEXT ${pattern})
        set(TMPV "${TMPV};${NEXT}")
    endforeach()
    set(${VAR} "${TMPV}" PARENT_SCOPE)
endfunction()

include_directories(AFTER "${SRCDIR}")
add_definitions(-DHAVE_BOOL)
if (MSVC)
  include_directories(AFTER "msinttypes")
  add_definitions(-D_UNICODE -DUNICODE)
endif (MSVC)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_definitions(-D__unix)
    add_definitions(-D__linux)
endif()

if ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
  add_definitions(-DBIT64)
endif ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")

set(LIBZPAQ_SRC "${SRCDIR}/contrib/zpaq/libzpaq.cpp")

set(PZPIPE_SRC "${SRCDIR}/pzpipe.cpp")

set(PZPIPE_UTILS_SRC "${SRCDIR}/pzpipe_utils.cpp")

set(PZPIPE_IO_SRC "${SRCDIR}/pzpipe_io.cpp")

add_executable(pzpipe ${LIBZPAQ_SRC} ${PZPIPE_UTILS_SRC} ${PZPIPE_IO_SRC} ${PZPIPE_SRC})

install(TARGETS pzpipe DESTINATION bin)
