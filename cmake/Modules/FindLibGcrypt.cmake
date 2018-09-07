#.rst:
# FindLibGcrypt
# -------------
#
# Try to find libgcrypt.
#
# This will define the following variables:
#
# ``LibGcrypt_FOUND``
#     True if libgcrypt is available.
#
# ``LibGcrypt_VERSION``
#     The version of LibGcrypt
#
# ``LibGcrypt_INCLUDE_DIRS``
#     This should be passed to target_include_directories() if
#     the target is not used for linking
#
# ``LibGcrypt_LIBRARIES``
#     This can be passed to target_link_libraries() instead of
#     the ``LibGcrypt::LibGcrypt`` target
#
# If ``LibGcrypt_FOUND`` is TRUE, the following imported target
# will be available:
#
# ``LibGcrypt::LibGcrypt``
#     The libgcrypt library
#
# Since 1.9.50.

#=============================================================================
# Copyright 2007 Charles Connell <charles@connells.org> (This was based upon FindKopete.cmake)
# Copyright 2010 Joris Guisson <joris.guisson@gmail.com>
# Copyright 2016 Christophe Giboudeaux <cgiboudeaux@gmx.com>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#=============================================================================

find_path(LibGcrypt_INCLUDE_DIRS
    NAMES gcrypt.h
    PATH_SUFFIXES libgcrypt
)

find_library(LibGcrypt_LIBRARIES
    NAMES gcrypt
)

if(MSVC)
    find_library(LibGcrypt_LIBRARIES_DEBUG
        NAMES gcryptd
    )

    if(NOT LibGcrypt_LIBRARIES_DEBUG)
        unset(LibGcrypt_LIBRARIES CACHE)
    endif()

    if(MSVC_IDE)
        if(NOT (LibGcrypt_LIBRARIES_DEBUG AND LibGcrypt_LIBRARIES))
            message(STATUS
                "\nCould NOT find the debug AND release version of the libgcrypt library.\n
                You need to have both to use MSVC projects.\n
                Please build and install both libgcrypt libraries first.\n"
            )
            unset(LibGcrypt_LIBRARIES CACHE)
        endif()
    else()
        string(TOLOWER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE_TOLOWER)
        if(CMAKE_BUILD_TYPE_TOLOWER MATCHES debug)
            set(LibGcrypt_LIBRARIES ${LibGcrypt_LIBRARIES_DEBUG})
        endif()
    endif()
endif()

# Get version from gcrypt.h
# #define GCRYPT_VERSION "1.6.4"
if(LibGcrypt_INCLUDE_DIRS AND LibGcrypt_LIBRARIES)
    file(STRINGS ${LibGcrypt_INCLUDE_DIRS}/gcrypt.h _GCRYPT_H REGEX "^#define GCRYPT_VERSION[ ]+.*$")
    string(REGEX REPLACE "^.*GCRYPT_VERSION[ ]+\"([0-9]+).([0-9]+).([0-9]+).*\".*$" "\\1" LibGcrypt_MAJOR_VERSION "${_GCRYPT_H}")
    string(REGEX REPLACE "^.*GCRYPT_VERSION[ ]+\"([0-9]+).([0-9]+).([0-9]+).*\".*$" "\\2" LibGcrypt_MINOR_VERSION "${_GCRYPT_H}")
    string(REGEX REPLACE "^.*GCRYPT_VERSION[ ]+\"([0-9]+).([0-9]+).([0-9]+).*\".*$" "\\3" LibGcrypt_PATCH_VERSION "${_GCRYPT_H}")

    set(LibGcrypt_VERSION "${LibGcrypt_MAJOR_VERSION}.${LibGcrypt_MINOR_VERSION}.${LibGcrypt_PATCH_VERSION}")
    unset(_GCRYPT_H)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibGcrypt
    FOUND_VAR LibGcrypt_FOUND
    REQUIRED_VARS LibGcrypt_INCLUDE_DIRS LibGcrypt_LIBRARIES
    VERSION_VAR LibGcrypt_VERSION
)

if(LibGcrypt_FOUND AND NOT TARGET LibGcrypt::LibGcrypt)
    add_library(LibGcrypt::LibGcrypt UNKNOWN IMPORTED)
    set_target_properties(LibGcrypt::LibGcrypt PROPERTIES
        IMPORTED_LOCATION "${LibGcrypt_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${LibGcrypt_INCLUDE_DIRS}")
endif()

mark_as_advanced(LibGcrypt_INCLUDE_DIRS LibGcrypt_LIBRARIES)

include(FeatureSummary)
set_package_properties(LibGcrypt PROPERTIES
    URL "http://directory.fsf.org/wiki/Libgcrypt"
    DESCRIPTION "General purpose crypto library based on the code used in GnuPG."
)
