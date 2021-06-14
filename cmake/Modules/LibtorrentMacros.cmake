# Various helper functions and macros for building libtorrent
include(FeatureSummary)

# function for issuing option() and add_feature_info() in a single call.
# Synopsis:
# feature_option(<option/feature name> <description> <default value>)
function(feature_option _name _description _default)
	string(CONCAT _desc "${_description} (default: ${_default})")
	option("${_name}" "${_desc}" "${_default}")
	add_feature_info("${_name}" "${_name}" "${_desc}")
endfunction()

# Set common variables and create some interface-only library targets
# that project targets can link to, either directly or transitively,
# to consume common compile options/definitions
macro(libtorrent_common_config)

	# treat value specified by the CXX_STANDARD target property as a requirement by default
	set(CMAKE_CXX_STANDARD_REQUIRED ON)

	add_library(libtorrent_common_cfg INTERFACE)

	if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		target_compile_options(libtorrent_common_cfg INTERFACE
			-Weverything
			-Wno-c++98-compat-pedantic
			-Wno-documentation
			-Wno-exit-time-destructors
			-Wno-global-constructors
			-Wno-padded
			-Wno-weak-vtables
		)
	elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
		target_compile_options(libtorrent_common_cfg INTERFACE
			-Wall
			-Wc++11-compat
			-Wextra
			-Wno-format-zero-length
			-Wparentheses
			-Wpedantic
			-Wvla
			-ftemplate-depth=512
		)
	elseif (MSVC)
		target_compile_options(libtorrent_common_cfg INTERFACE
			/utf-8
			# https://devblogs.microsoft.com/cppblog/msvc-now-correctly-reports-__cplusplus/
			/Zc:__cplusplus
			/W4
			# C4251: 'identifier' : class 'type' needs to have dll-interface to be
			#        used by clients of class 'type2'
			/wd4251
			# C4268: 'identifier' : 'const' static/global data initialized
			#        with compiler generated default constructor fills the object with zeros
			/wd4268
			# C4275: non DLL-interface classkey 'identifier' used as base for
			#        DLL-interface classkey 'identifier'
			/wd4275
			# C4373: virtual function overrides, previous versions of the compiler
			#        did not override when parameters only differed by const/volatile qualifiers
			/wd4373
			# C4503: 'identifier': decorated name length exceeded, name was truncated
			/wd4503
		)
	endif()

	target_compile_definitions(libtorrent_common_cfg INTERFACE
		# target Windows Vista or later
		"$<$<BOOL:${WIN32}>:NTDDI_VERSION=0x06000000;_WIN32_WINNT=0x0600>"
	)

endmacro()

# check if we need to link against libatomic (not needed on MSVC)
macro(libtorrent_ensure_link_libatomic)

	if (NOT WIN32)
		# TODO: migrate to CheckSourceCompiles in CMake >= 3.19
		include(CheckCXXSourceCompiles)

		set(ATOMICS_TEST_SOURCE [=[
			#include <atomic>
			#include <cstdint>
			std::atomic<int> x{0};
			int main() {
				x.fetch_add(1, std::memory_order_relaxed);
				return 0;
			}
		]=])
		string(REPLACE "std::atomic<int>" "std::atomic<std::int64_t>" ATOMICS64_TEST_SOURCE "${ATOMICS_TEST_SOURCE}")

		if (APPLE)
			set(CMAKE_REQUIRED_FLAGS "-std=c++11")
		endif()
		check_cxx_source_compiles("${ATOMICS_TEST_SOURCE}" HAVE_CXX_ATOMICS_WITHOUT_LIB)
		check_cxx_source_compiles("${ATOMICS64_TEST_SOURCE}" HAVE_CXX_ATOMICS64_WITHOUT_LIB)
		if ((NOT HAVE_CXX_ATOMICS_WITHOUT_LIB) OR (NOT HAVE_CXX_ATOMICS64_WITHOUT_LIB))
			set(CMAKE_REQUIRED_LIBRARIES "atomic")
			check_cxx_source_compiles("${ATOMICS_TEST_SOURCE}" HAVE_CXX_ATOMICS_WITH_LIB)
			check_cxx_source_compiles("${ATOMICS64_TEST_SOURCE}" HAVE_CXX_ATOMICS64_WITH_LIB)
			if ((NOT HAVE_CXX_ATOMICS_WITH_LIB) OR (NOT HAVE_CXX_ATOMICS64_WITH_LIB))
				message(FATAL_ERROR "No native support for std::atomic, or libatomic not found!")
			else()
				message(STATUS "Linking with libatomic for atomics support")
				unset(CMAKE_REQUIRED_LIBRARIES)
				target_link_libraries(torrent-rasterbar PUBLIC atomic)
			endif()
		endif()
		if (APPLE)
			unset(CMAKE_REQUIRED_FLAGS)
		endif()
	endif()

endmacro()

# This is best effort attempt to propagate whether the library was built with
# C++11 or not. It affects the ABI of entry. A client building with C++14 and
# linking against a libtorrent binary built with C++11 can still define
# TORRENT_CXX11_ABI
macro(libtorrent_cxx11_abi_issue_workaround)

	if ("${CMAKE_CXX_STANDARD}" STREQUAL "11")
		target_compile_definitions(torrent-rasterbar PUBLIC TORRENT_CXX11_ABI)
	endif()

endmacro()
