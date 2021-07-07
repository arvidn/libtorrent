# This module provides generate_and_install_pkg_config_file() function.
# The function takes target name and expects a fully configured project, i.e. with set version and description.
# The function extracts interface libraries, include dirs, definitions and compile options
# from the target and generates pkg-config file with install() command
# Arbitrary generator expressions are supported in any target property.
# A limitation of this function is that it assumes that (and will only work when)
# the set of link libraries of the target is exclusively comprised of library target names (such as Foo::Bar),
# and not of any other type of input accepted by target_link_libraries()
# (i.e. full paths to libraries, plain library names, link flags, or legacy keywords)
# This should not be an issue if the project follows modern CMake best practices.

# The pkg-config generation process consists in 2 parts:
# 1. The first part consists of 3 steps:
#   - during the configuration phase of the CMake build,
#     a `compile-settings.cmake` file is created with the necessary values
#     from a template.
#   - also during the configuration phase of the CMake build,
#     a `generate-pkg-config.cmake` file is created from a template
#   - during the generation phase of the CMake build,
#     a `compile-settings-expanded.cmake` file is generated, by resolving the
#	  generator expressions present in the `compile-settings.cmake` file
#     created in the first step.
# 2. At install time, the `generate-pkg-config.cmake` script previously created
#  is executed. It takes some of its inputs from `compile-settings-expanded.cmake`,
#  which was also generated in the previous part.
#  As a result, the pkg-config file is finally created and copied to the install location

# A convenince target, create_pkgconfig, is provided to create the pkgconfig file
# in the build directory without having to build the whole project or executing the
# generated creation script (`generate-pkg-config.cmake`) manually.

# save the current file dir for later use in the generate_and_install_pkg_config_file() function
set(_GeneratePkGConfigDir "${CMAKE_CURRENT_LIST_DIR}/GeneratePkgConfig")

include(GNUInstallDirs)

# This helper function looks at the list of interface link libraries of a target
# and returns a modified list where each generator expression related to each
# target is replaced with a generator expression that extracts the desired
# property from that target. Because arbitrary generator expressions may be
# encountered during processing, CMake may trucate list elements early
# when they comtain ';' characters (as in the case of a generator expression that can evaluate to a list)
# Thus, care is taken to keep '<' and '>' characters balanced in each list element
# to ensure no generator expression is broken with the following algorithm:
# foreach() element in list,
# 	if element is balanced w.r.t. < and >, transform and append
# 	if not, prepend to next element, which is certain to be the next fragment of the generator expression
# endforeach()
#
# OUT_LIST_VAR: variable where the output list will be stored
# TARGET: the target whose interface link libraries will be inspected
# LINKED_TARGETS_PROPERTY: the property that we want to get from the interface link libraries of the TARGET
function(get_target_prop_genexps_from_interface_link_libraries)

	set(options "")
	set(oneValueArgs OUT_LIST_VAR TARGET LINKED_TARGETS_PROPERTY)
	set(multiValueArgs "")

	cmake_parse_arguments(
		PARSE_ARGV 0
		ARG
		"${options}" "${oneValueArgs}" "${multiValueArgs}"
	)

	# this is so that the TARGET_PROPERTY genexp doesn't fail due to empty target
	if (NOT TARGET dummy)
		add_library(dummy INTERFACE)
	endif()

	get_target_property(_iface_link_libs "${ARG_TARGET}" INTERFACE_LINK_LIBRARIES)
	foreach(_el IN LISTS _iface_link_libs)

		if(_concat_flag)
			set(_el "${_temp}${_el}")
			set(_concat_flag OFF)
		endif()

		string(LENGTH "${_el}" str_len)
		math(EXPR last_char_index "${str_len} - 1")
		set(_open 0)
		set(_close 0)

		# iterate over string to find if < and > are balanced
		foreach (char_index RANGE 0 ${last_char_index} 1)
			string(SUBSTRING "${_el}" "${char_index}" "1" char)
			if (char STREQUAL "<")
				math(EXPR _open "${_open} + 1")
			elseif(char STREQUAL ">")
				math(EXPR _close "${_close} + 1")
			endif()
		endforeach()

		# special case for escaped ">"
		string(REGEX MATCHALL "\\$<ANGLE-R>" _angle_r "${_el}")
		list(LENGTH _angle_r _angle_r_count)
		math(EXPR _close "${_close} + ${_angle_r_count}")

		if (_open EQUAL _close)
			list(APPEND _result "$<TARGET_PROPERTY:$<IF:$<BOOL:${_el}>,${_el},dummy>,${ARG_LINKED_TARGETS_PROPERTY}>")
		else()
			set(_concat_flag ON)
			set(_temp "${_el}")
		endif()
	endforeach()

	if (NOT ARG_LINKED_TARGETS_PROPERTY STREQUAL "LOCATION")
		get_target_property(_own_props "${ARG_TARGET}" "${ARG_LINKED_TARGETS_PROPERTY}")
		if(_own_props)
			list(APPEND _result "${_own_props}")
		endif()
	endif()

	# HACK: for static libraries cmake populates link dependencies as $<LINK_ONLY:lib_name>.
	# pkg-config does not support special handling for static libraries and as such we will remove
	# that generator expression
	string(REPLACE "$<LINK_ONLY:" "$<1:" _result "${_result}")
	# HACK: file(GENERATE), which we use for expanding generator expressions, is BUILD_INTERFACE,
	# but we need INSTALL_INTERFACE here.
	# See https://gitlab.kitware.com/cmake/cmake/issues/17984
	string(REPLACE "$<BUILD_INTERFACE:" "$<0:" _result "${_result}")
	string(REPLACE "$<INSTALL_INTERFACE:" "${CMAKE_INSTALL_PREFIX}/$<1:" _result "${_result}")

	set("${ARG_OUT_LIST_VAR}" "${_result}" PARENT_SCOPE)
endfunction()

# Generates and installs a pkg-config file for a given target
function(generate_and_install_pkg_config_file _target _packageName)

	get_target_property(_output_name ${_target} OUTPUT_NAME)
	if (NOT _output_name)
		set(_output_name "${_target}")
	endif()
	set(_package_name "${_packageName}")
	set(_pkg_config_template_file "${_GeneratePkGConfigDir}/pkg-config.cmake.in")
	set(_generate_target_dir "${CMAKE_CURRENT_BINARY_DIR}/${_target}-pkgconfig")
	set(_genexp_variables_file "${_generate_target_dir}/compile-settings.cmake")

	# collect the necessary interface target properties
	get_target_prop_genexps_from_interface_link_libraries(
		OUT_LIST_VAR _interface_link_libraries
		TARGET "${_target}"
		LINKED_TARGETS_PROPERTY LOCATION
	)

	get_target_prop_genexps_from_interface_link_libraries(
		OUT_LIST_VAR _interface_include_dirs
		TARGET "${_target}"
		LINKED_TARGETS_PROPERTY INTERFACE_INCLUDE_DIRECTORIES
	)

	get_target_prop_genexps_from_interface_link_libraries(
		OUT_LIST_VAR _interface_link_options
		TARGET "${_target}"
		LINKED_TARGETS_PROPERTY INTERFACE_LINK_OPTIONS
	)

	get_target_prop_genexps_from_interface_link_libraries(
		OUT_LIST_VAR _interface_compile_definitions
		TARGET "${_target}"
		LINKED_TARGETS_PROPERTY INTERFACE_COMPILE_DEFINITIONS
	)

	get_target_prop_genexps_from_interface_link_libraries(
		OUT_LIST_VAR _interface_compile_options
		TARGET "${_target}"
		LINKED_TARGETS_PROPERTY INTERFACE_COMPILE_OPTIONS
	)

	# HACK: before CMake 3.19 file(GENERATE) can't process input that contains
	# target-dependent generator expressions, such as $<CXX_COMPILER_ID:...>
	# or some that Threads::Threads propagates to us via its INTERFACE_COMPILE_OPTIONS.
	# See #4956 and CMake bug #21074.
	# For each problematic case, we must workaround manually.
	if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.19)
		set(_target_arg TARGET ${_target})
	else()
		string(REPLACE
			"<COMPILE_LANG_AND_ID:CUDA,NVIDIA>"
			"<COMPILE_LANGUAGE:CUDA>"
			_interface_compile_options
			"${_interface_compile_options}"
		)
		string(REPLACE
			",$<CXX_COMPILER_ID:GNU>>:_GLIBCXX_DEBUG;_GLIBCXX_DEBUG_PEDANTIC>;"
			",1>:_GLIBCXX_DEBUG;_GLIBCXX_DEBUG_PEDANTIC>;"
			_interface_compile_definitions
			"${_interface_compile_definitions}"
		)
	endif()

	# put non-expanded target and project properties into a file
	configure_file(
		"${_GeneratePkGConfigDir}/target-compile-settings.cmake.in"
		"${_genexp_variables_file}"
		@ONLY
	)

	# generate file with expanded target and project properties
	# and configure the script that will use the generated file to
	# create the pkg-config file at install-time
	get_property(_isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
	if (NOT _isMultiConfig)
		set(_pkg_config_generation_script "${_generate_target_dir}/generate-pkg-config.cmake")
		set(_expanded_variables_file "${_generate_target_dir}/compile-settings-expanded.cmake")

		file(GENERATE
			OUTPUT "${_expanded_variables_file}"
			INPUT "${_genexp_variables_file}"
			${_target_arg}
		)
		configure_file(
			"${_GeneratePkGConfigDir}/generate-pkg-config.cmake.in"
			"${_generate_target_dir}/generate-pkg-config.cmake"
			@ONLY
		)

		install(SCRIPT "${_generate_target_dir}/generate-pkg-config.cmake")
	else()
		set(_pkg_config_generation_script "${_generate_target_dir}/$<CONFIG>/generate-pkg-config.cmake")
		foreach (cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
			set(_expanded_variables_file "${_generate_target_dir}/${cfg}/compile-settings-expanded.cmake")

			file(GENERATE
				OUTPUT "${_expanded_variables_file}"
				INPUT "${_genexp_variables_file}"
				CONDITION "$<CONFIG:${cfg}>"
				${_target_arg}
			)
			configure_file(
				"${_GeneratePkGConfigDir}/generate-pkg-config.cmake.in"
				"${_generate_target_dir}/${cfg}/generate-pkg-config.cmake"
				@ONLY
			)

			install(SCRIPT "${_generate_target_dir}/$<CONFIG>/generate-pkg-config.cmake")
		endforeach()
	endif()

	add_custom_target(create_pkgconfig
		COMMENT "Create pkg-config file in build directory (useful for debugging)"
		COMMAND "${CMAKE_COMMAND}"
			-D "CMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}"
			-D "PKGCONFIG_SKIP_INSTALL=ON"
			-P "${_pkg_config_generation_script}"
		VERBATIM
	)
endfunction()
