# taken from https://github.com/onqtam/ucm/blob/master/cmake/ucm.cmake

#
# ucm.cmake - useful cmake macros
#
# Copyright (c) 2016 Viktor Kirilov
#
# Distributed under the MIT Software License
# See accompanying file LICENSE.txt or copy at
# https://opensource.org/licenses/MIT
#
# The documentation can be found at the library's page:
# https://github.com/onqtam/ucm

include(CMakeParseArguments)

# ucm_gather_flags
# Gathers all lists of flags for printing or manipulation
macro(ucm_gather_flags with_linker result)
	set(${result} "")
	# add the main flags without a config
	list(APPEND ${result} CMAKE_C_FLAGS)
	list(APPEND ${result} CMAKE_CXX_FLAGS)
	if(${with_linker})
		list(APPEND ${result} CMAKE_EXE_LINKER_FLAGS)
		list(APPEND ${result} CMAKE_MODULE_LINKER_FLAGS)
		list(APPEND ${result} CMAKE_SHARED_LINKER_FLAGS)
		list(APPEND ${result} CMAKE_STATIC_LINKER_FLAGS)
	endif()

	if("${CMAKE_CONFIGURATION_TYPES}" STREQUAL "" AND NOT "${CMAKE_BUILD_TYPE}" STREQUAL "")
		# handle single config generators - like makefiles/ninja - when CMAKE_BUILD_TYPE is set
		string(TOUPPER ${CMAKE_BUILD_TYPE} config)
		list(APPEND ${result} CMAKE_C_FLAGS_${config})
		list(APPEND ${result} CMAKE_CXX_FLAGS_${config})
		if(${with_linker})
			list(APPEND ${result} CMAKE_EXE_LINKER_FLAGS_${config})
			list(APPEND ${result} CMAKE_MODULE_LINKER_FLAGS_${config})
			list(APPEND ${result} CMAKE_SHARED_LINKER_FLAGS_${config})
			list(APPEND ${result} CMAKE_STATIC_LINKER_FLAGS_${config})
		endif()
	else()
		# handle multi config generators (like msvc, xcode)
		foreach(config ${CMAKE_CONFIGURATION_TYPES})
			string(TOUPPER ${config} config)
			list(APPEND ${result} CMAKE_C_FLAGS_${config})
			list(APPEND ${result} CMAKE_CXX_FLAGS_${config})
			if(${with_linker})
				list(APPEND ${result} CMAKE_EXE_LINKER_FLAGS_${config})
				list(APPEND ${result} CMAKE_MODULE_LINKER_FLAGS_${config})
				list(APPEND ${result} CMAKE_SHARED_LINKER_FLAGS_${config})
				list(APPEND ${result} CMAKE_STATIC_LINKER_FLAGS_${config})
			endif()
		endforeach()
	endif()
endmacro()

# ucm_set_runtime
# Sets the runtime (static/dynamic) for msvc/gcc
macro(ucm_set_runtime)
	cmake_parse_arguments(ARG "STATIC;DYNAMIC" "" "" ${ARGN})

	if(ARG_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR "unrecognized arguments: ${ARG_UNPARSED_ARGUMENTS}")
	endif()

	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" STREQUAL "")
		message(AUTHOR_WARNING "ucm_set_runtime() does not support clang yet!")
	endif()

	ucm_gather_flags(0 flags_configs)

	# add/replace the flags
	# note that if the user has messed with the flags directly this function might fail
	# - for example if with MSVC and the user has removed the flags - here we just switch/replace them
	if("${ARG_STATIC}")
		foreach(flags ${flags_configs})
			if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
				if(NOT ${flags} MATCHES "-static-libstdc\\+\\+")
					set(${flags} "${${flags}} -static-libstdc++")
				endif()
				if(NOT ${flags} MATCHES "-static-libgcc")
					set(${flags} "${${flags}} -static-libgcc")
				endif()
			elseif(MSVC)
				if(${flags} MATCHES "/MD")
					string(REGEX REPLACE "/MD" "/MT" ${flags} "${${flags}}")
				endif()
			endif()
		endforeach()
	elseif("${ARG_DYNAMIC}")
		foreach(flags ${flags_configs})
			if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
				if(${flags} MATCHES "-static-libstdc\\+\\+")
					string(REGEX REPLACE "-static-libstdc\\+\\+" "" ${flags} "${${flags}}")
				endif()
				if(${flags} MATCHES "-static-libgcc")
					string(REGEX REPLACE "-static-libgcc" "" ${flags} "${${flags}}")
				endif()
			elseif(MSVC)
				if(${flags} MATCHES "/MT")
					string(REGEX REPLACE "/MT" "/MD" ${flags} "${${flags}}")
				endif()
			endif()
		endforeach()
	endif()
endmacro()

# ucm_print_flags
# Prints all compiler flags for all configurations
macro(ucm_print_flags)
	ucm_gather_flags(1 flags_configs)
	message("")
	foreach(flags ${flags_configs})
		message("${flags}: ${${flags}}")
	endforeach()
	message("")
endmacro()
