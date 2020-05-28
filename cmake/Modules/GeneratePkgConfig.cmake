# This module provides generate_and_install_pkg_config_file() function.
# The function takes target name and expects a fully configured project, i.e. with set version and
# description. The function extracts interface libraries, include dirs, definitions and options
# from the target and generates pkg-config file with install() command
# The function expands imported targets and generator expressions

# save the current file dir for later use in the generate_and_install_pkg_config_file() function
set(_GeneratePkGConfigDir "${CMAKE_CURRENT_LIST_DIR}/GeneratePkgConfig")

include(GNUInstallDirs)

function(_compile_features_to_gcc_flags _res _features)
	set(features ${_features})
	# leave only cxx_std_nn items
	list(FILTER features INCLUDE REGEX cxx_std_)
	if (${features} STREQUAL "")
		set(${_res} "" PARENT_SCOPE)
	else()
		# if there are more than a single cxx_std_nn feature...
		list(SORT features)
		# take the most recent standard, i.e. the last element
		list(GET features -1 standard)
		# cmake calls it cxx_std_98, but we (obviously) want -std=c++03
		string(REPLACE 98 03 standard "${standard}")
		string(REPLACE cxx_std_ -std=c++ standard "${standard}")
		set(${_res} "${standard}" PARENT_SCOPE)
	endif()
endfunction()

function(_get_target_property_merging_configs _var_name _target_name _propert_name)
	get_property(prop_set TARGET ${_target_name} PROPERTY ${_propert_name} SET)
	if (prop_set)
		get_property(vals TARGET ${_target_name} PROPERTY ${_propert_name})
	else()
		if (CMAKE_BUILD_TYPE)
			list(APPEND configs ${CMAKE_BUILD_TYPE})
		elseif(CMAKE_CONFIGURATION_TYPES)
			list(APPEND configs ${CMAKE_CONFIGURATION_TYPES})
		endif()
		foreach(cfg ${configs})
			string(TOUPPER "${cfg}" UPPERCFG)
			get_property(mapped_configs TARGET ${_target_name} PROPERTY "MAP_IMPORTED_CONFIG_${UPPERCFG}")
			if (mapped_configs)
				list(GET "${mapped_configs}" 0 target_cfg)
			else()
				set(target_cfg "${UPPERCFG}")
			endif()
			get_property(prop_set TARGET ${_target_name} PROPERTY ${_propert_name}_${target_cfg} SET)
			if (prop_set)
				get_property(val_for_cfg TARGET ${_target_name} PROPERTY ${_propert_name}_${target_cfg})
				list(APPEND vals "$<$<CONFIG:${cfg}>:${val_for_cfg}>")
				break()
			endif()
		endforeach()
		if (NOT prop_set)
			get_property(imported_cfgs TARGET ${_target_name} PROPERTY IMPORTED_CONFIGURATIONS)
			# CMake docs say we can use any of the imported configs
			list(GET imported_cfgs 0 imported_config)
			get_property(vals TARGET ${_target_name} PROPERTY ${_propert_name}_${imported_config})
			# remove config generator expression. Only in this case! Notice we use such expression
			# ourselves in the loop above
			string(REPLACE "$<$<CONFIG:${imported_config}>:" "$<1:" vals "${vals}")
		endif()
	endif()
	# HACK for static libraries cmake populates link dependencies as $<LINK_ONLY:lib_name>.
	# pkg-config does not support special handling for static libraries and as such we will remove
	# that generator expression
	string(REPLACE "$<LINK_ONLY:" "$<1:" vals "${vals}")
	# HACK file(GENERATE), which we use for expanding generator expressions, is BUILD_INTERFACE,
	# but we need INSTALL_INTERFACE here.
	# See https://gitlab.kitware.com/cmake/cmake/issues/17984
	string(REPLACE "$<BUILD_INTERFACE:" "$<0:" vals "${vals}")
	string(REPLACE "$<INSTALL_INTERFACE:" "@CMAKE_INSTALL_PREFIX@/$<1:" vals "${vals}")
	set(${_var_name} "${vals}" PARENT_SCOPE)
endfunction()

# This helper function expands imported targets from the provided targets list, collecting their
# interface link libraries and imported locations, include directories, compile options and definitions
# into the specified variables
function(_expand_targets _targets _libraries_var _include_dirs_var _compile_options_var _compile_definitions_var)
	set(_any_target_was_expanded True)
	set(_libs "${${_libraries_var}}")
	set(_includes "${${_include_dirs_var}}")
	set(_defs "${${_compile_definitions_var}}")
	set(_options "${${_compile_options_var}}")

	list(APPEND _libs "${_targets}")

	while(_any_target_was_expanded)
		set(_any_target_was_expanded False)
		set(_new_libs "")
		foreach (_dep ${_libs})
			if (TARGET ${_dep})
				set(_any_target_was_expanded True)

				get_target_property(_type ${_dep} TYPE)
				if ("${_type}" STREQUAL "INTERFACE_LIBRARY")
					# this library may not have IMPORTED_LOCATION property
					set(_imported_location "")
				else()
					_get_target_property_merging_configs(_imported_location ${_dep} IMPORTED_LOCATION)
				endif()

				_get_target_property_merging_configs(_iface_link_libraries ${_dep} INTERFACE_LINK_LIBRARIES)
				_get_target_property_merging_configs(_iface_include_dirs ${_dep} INTERFACE_INCLUDE_DIRECTORIES)
				_get_target_property_merging_configs(_iface_compile_options ${_dep} INTERFACE_COMPILE_OPTIONS)
				_get_target_property_merging_configs(_iface_definitions ${_dep} INTERFACE_COMPILE_DEFINITIONS)
				get_target_property(_iface_compile_features ${_dep} INTERFACE_COMPILE_FEATURES)

				if (_imported_location)
					list(APPEND _new_libs "${_imported_location}")
				endif()

				if (_iface_link_libraries)
					list(APPEND _new_libs "${_iface_link_libraries}")
				endif()

				if(_iface_include_dirs)
					list(APPEND _includes "${_iface_include_dirs}")
				endif()

				if(_iface_compile_options)
					list(APPEND _options "${_iface_compile_options}")
				endif()

				if (_iface_compile_features)
					_compile_features_to_gcc_flags(features_flags ${_iface_compile_features})
					list(APPEND _options ${features_flags})
				endif()

				if(_iface_definitions)
					list(APPEND _defs "${_iface_definitions}")
				endif()

				list(REMOVE_DUPLICATES _new_libs)
				list(REMOVE_DUPLICATES _includes)
				# Options order is important, thus leave duplicates as they are, skipping duplicates removal
				list(REMOVE_DUPLICATES _defs)
			else()
				list(APPEND _new_libs "${_dep}")
			endif()
		endforeach()
		set(_libs "${_new_libs}")
	endwhile()
	set(${_libraries_var} "${_libs}" PARENT_SCOPE)
	set(${_include_dirs_var} "${_includes}" PARENT_SCOPE)
	set(${_compile_options_var} "${_options}" PARENT_SCOPE)
	set(${_compile_definitions_var} "${_defs}" PARENT_SCOPE)
endfunction()

# Generates and installs a pkg-config file for a given target
function(generate_and_install_pkg_config_file _target _packageName)
	# collect target properties
	_expand_targets(${_target}
		_interface_link_libraries _interface_include_dirs
		_interface_compile_options _interface_definitions)

	get_target_property(_output_name ${_target} OUTPUT_NAME)
	if (NOT _output_name)
		set(_output_name "${_target}")
	endif()

	set(_package_name "${_packageName}")

	# remove standard include directories
	foreach(d IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
		list(REMOVE_ITEM _interface_include_dirs "${d}")
	endforeach()

	set(_generate_target_dir "${CMAKE_CURRENT_BINARY_DIR}/${_target}-pkgconfig")
	set(_pkg_config_file_template_filename "${_GeneratePkGConfigDir}/pkg-config.cmake.in")

	# put target and project properties into a file
	configure_file("${_GeneratePkGConfigDir}/target-compile-settings.cmake.in"
		"${_generate_target_dir}/compile-settings.cmake" @ONLY)

	get_property(_isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
	if (NOT _isMultiConfig)
		set(_variables_file_name "${_generate_target_dir}/compile-settings-expanded.cmake")

		file(GENERATE OUTPUT "${_variables_file_name}" INPUT "${_generate_target_dir}/compile-settings.cmake")

		configure_file("${_GeneratePkGConfigDir}/generate-pkg-config.cmake.in"
			"${_generate_target_dir}/generate-pkg-config.cmake" @ONLY)

		install(SCRIPT "${_generate_target_dir}/generate-pkg-config.cmake")
	else()
		foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
			set(_variables_file_name "${_generate_target_dir}/${cfg}/compile-settings-expanded.cmake")

			file(GENERATE OUTPUT "${_variables_file_name}" INPUT "${_generate_target_dir}/compile-settings.cmake" CONDITION "$<CONFIG:${cfg}>")

			configure_file("${_GeneratePkGConfigDir}/generate-pkg-config.cmake.in"
				"${_generate_target_dir}/${cfg}/generate-pkg-config.cmake" @ONLY)

			install(SCRIPT "${_generate_target_dir}/${cfg}/generate-pkg-config.cmake")
		endforeach()
	endif()
endfunction()
