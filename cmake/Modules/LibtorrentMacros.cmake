# Various helper function and macros for building libtorrent

include(FeatureSummary)

# macro for issuing option() and add_feature_info() in a single call.
# Synopsis:
# feature_option(<option_and_feature_name> <option_and_feature_description> <default_option_value>)
macro(feature_option _name _description _default)
	option(${_name} "${_description}" ${_default})
	add_feature_info(${_name} ${_name} "${_description}")
endmacro()

# function to add a simple build option which controls compile definition(s) for a target.
# Synopsis:
# target_optional_compile_definitions(<target> [FEATURE]
#   NAME <name> DESCRIPTION <description> DEFAULT <default_value>
#   [ENABLED [enabled_compile_definitions...]]
#   [DISABLED [disabled_compile_defnitions...]]
# )
# NAME, DESCRIPTION and DEFAULT are passed to option() call
# if FEATURE is given, they are passed to add_feature_info()
# ENABLED lists compile definitions that will be set on <target> when option is enabled,
# DISABLED lists definitions that will be set otherwise
function(target_optional_compile_definitions _target _scope)
	set(options FEATURE)
	set(oneValueArgs NAME DESCRIPTION DEFAULT)
	set(multiValueArgs ENABLED DISABLED)
	cmake_parse_arguments(TOCD ${options} "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
	option(${TOCD_NAME} "${TOCD_DESCRIPTION}" ${TOCD_DEFAULT})
	if (${${TOCD_NAME}})
		target_compile_definitions(${_target} ${_scope} ${TOCD_ENABLED})
	else()
		target_compile_definitions(${_target} ${_scope} ${TOCD_DISABLED})
	endif()
	if(${TOCD_FEATURE})
		add_feature_info(${TOCD_NAME} ${TOCD_NAME} "${TOCD_DESCRIPTION}")
	endif()
endfunction()

# a helper macro that calls find_package() and appends the package (if found) to the
# _package_dependencies list, which can be used later to generate package config file
macro(find_public_dependency _name)
	find_package(${_name} ${ARGN})
	string(TOUPPER "${_name}" _name_uppercased)
	if (${_name}_FOUND OR ${_name_uppercased}_FOUND)
		# Dependencies to be used below for generating Config.cmake file
		# We don't need the 'REQUIRED' argument there
		set(_args "${_name}")
		list(APPEND _args "${ARGN}")
		list(REMOVE_ITEM _args "REQUIRED")
		list(REMOVE_ITEM _args "") # just in case
		string(REPLACE ";" " " _args "${_args}")
		list(APPEND _package_dependencies "${_args}")
	endif()
endmacro()

function(_cxx_standard_to_year _yearVar _std)
	if (${_std} GREATER 97)
		math(EXPR _year "1900 + ${_std}")
	else()
		math(EXPR _year "2000 + ${_std}")
	endif()
	set(${_yearVar} ${_year} PARENT_SCOPE)
endfunction()

function(select_cxx_standard _target _minimal_supported_version)
	message(STATUS "Compiler default is C++${CMAKE_CXX_STANDARD_DEFAULT}")
	# make the CXX_STANDARD property public to ensure it makes it into the pkg-config file
	get_target_property(_std ${_target} CXX_STANDARD)
	# ${CMAKE_CXX_STANDARD_DEFAULT} could be 98, which is lower version than C++11. Thus we convert std
	# version to year value and compare years
	_cxx_standard_to_year(minimal_supported_version_year ${_minimal_supported_version})
	# if it is unset, select the default if it is sufficient or the ${_minimal_supported_version}
	if (NOT ${_std})
		_cxx_standard_to_year(std_default_year ${CMAKE_CXX_STANDARD_DEFAULT})
		if (${std_default_year} GREATER_EQUAL ${minimal_supported_version_year})
			set(_std ${CMAKE_CXX_STANDARD_DEFAULT})
		else()
			set(_std ${_minimal_supported_version})
		endif()
	else()
		_cxx_standard_to_year(std_year ${_std})
		if (${std_year} LESS ${minimal_supported_version_year})
			message(FATAL_ERROR "The minimal supported C++ standard version is C++${_minimal_supported_version}")
		endif()
	endif()

	if (cxx_std_${_std} IN_LIST CMAKE_CXX_COMPILE_FEATURES)
		# force this standard
		target_compile_features(${_target} PUBLIC cxx_std_${_std})
	else()
		message(FATAL_ERROR "Requested C++ standard version (${_std}) is not supported by the compiler")
	endif()

	message(STATUS "Building in C++${_std} mode")
endfunction()

# function for parsing version variables that are set in version.hpp file
# the version identifiers there are defined as follows:
# #define LIBTORRENT_VERSION_MAJOR 1
# #define LIBTORRENT_VERSION_MINOR 2
# #define LIBTORRENT_VERSION_TINY 0

function(read_version _verFile _outVarMajor _outVarMinor _outVarTiny)
	file(STRINGS ${_verFile} verFileContents REGEX ".+LIBTORRENT_VERSION_[A-Z]+.[0-9]+.*")
# 	message(STATUS "version file contents: ${verFileContents}")
	# the verFileContents variable contains something like the following:
	# #define LIBTORRENT_VERSION_MAJOR 1;#define LIBTORRENT_VERSION_MINOR 2;#define LIBTORRENT_VERSION_TINY 0
	set(_regex ".+_MAJOR +([0-9]+);.+_MINOR +([0-9]+);.+_TINY +([0-9]+)")
	 # note quotes around _regex, they are needed because the variable contains semicolons
	string(REGEX MATCH "${_regex}" _tmp "${verFileContents}")
	if (NOT _tmp)
		message(FATAL_ERROR "Could not detect project version number from ${_verFile}")
	endif()

# 	message(STATUS "Matched version string: ${_tmp}")

	set(${_outVarMajor} ${CMAKE_MATCH_1} PARENT_SCOPE)
	set(${_outVarMinor} ${CMAKE_MATCH_2} PARENT_SCOPE)
	set(${_outVarTiny} ${CMAKE_MATCH_3} PARENT_SCOPE)
endfunction()
