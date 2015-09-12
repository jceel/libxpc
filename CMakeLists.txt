#
# Copyright 2015 iXsystems, Inc.
# All rights reserved
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted providing that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES LOSS OF USE, DATA, OR PROFITS OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
# IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

cmake_minimum_required(VERSION 3.2)
project(libxpc)

set(BASE_SOURCES
    mpack.c
    xpc_array.c
    xpc_connection.c
    xpc_dictionary.c
    xpc_misc.c
    xpc_type.c
)

set(HEADERS
    activity.h
    base.h
    connection.h
    debug.h
    endpoint.h
    launchd.h
    xpc.h
)

set(UNIX_TRANSPORT_SOURCES
    transports/unix.c
)

set(MACH_TRANSPORT_SOURCES
    transports/mach.c
)

set(SOURCES
    ${BASE_SOURCES}
    ${UNIX_TRANSPORT_SOURCES}
)

option(XPC_DEBUG "Adds debugging output" OFF)

if(XPC_DEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0")
    add_definitions(-DXPC_DEBUG)
endif()

include_directories(/usr/local/include)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fblocks")
add_library(xpc SHARED ${SOURCES})
add_subdirectory(examples)
