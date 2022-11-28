cmake_minimum_required(VERSION 3.17.0 FATAL_ERROR) # Configurable policies: <= CMP0102

# To build python bindings we need a python executable and boost python module. Unfortunately,
# their names might not be interlinked and we can not implement a general solution.
# The code below assumes default boost installation, when the module for python 3 is named 'python3'.
# To customize that one can provide a name for the Boost::python module via
# 'boost-python-module-name' variable when invoking cmake.
# E.g. on Gentoo with python 3.7 and Boost::python library name 'libboost_python-3.7.so'
# the parameter would be -Dboost-python-module-name="python-3.7".

# The extension module and the cpython executable have to use the same C runtime library. On Windows
# Python is compiled with MSVC and we will test MSVC version to make sure that it is the same for
# the given Python version and our extension module. See https://wiki.python.org/moin/WindowsCompilers
# for details. We provide a flag to skip this test for whatever reason (pass -Dskip-python-runtime-test=True)

# Sets _ret to a list of python versions (major.minor) that use the same MSVC runtime as this build does
# assumes MSVC was detected already
# See https://en.wikipedia.org/wiki/Microsoft_Visual_C++#Internal_version_numbering
# See https://devguide.python.org/#status-of-python-branches for supported python versions
function(_get_compatible_python_versions _ret)
	if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19 AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20)
		list(APPEND _tmp 3.6 3.7 3.8 3.9 3.10 3.11)
	endif()
	set(${_ret} ${_tmp} PARENT_SCOPE)
endfunction()


if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC" AND NOT skip-python-runtime-test)
	_get_compatible_python_versions(Python_ADDITIONAL_VERSIONS)
endif()

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC" AND NOT skip-python-runtime-test)
	message(STATUS "Testing found python version. Requested: ${Python_ADDITIONAL_VERSIONS}, found: ${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}")
	if (NOT "${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}" IN_LIST Python_ADDITIONAL_VERSIONS)
		message(FATAL_ERROR "Incompatible Python and C runtime: MSVC ${CMAKE_CXX_COMPILER_VERSION} and Python ${Python3_VERSION}")
	endif()
endif()

set(boost-python-module-name "python${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}" CACHE STRING "Boost::python module name, e.g. 'python-3.7'")

find_package(Boost REQUIRED COMPONENTS ${boost-python-module-name})

Python3_add_library(python-libtorrent MODULE WITH_SOABI
	src/alert.cpp
	src/converters.cpp
	src/create_torrent.cpp
	src/datetime.cpp
	src/entry.cpp
	src/error_code.cpp
	src/fingerprint.cpp
	src/info_hash.cpp
	src/ip_filter.cpp
	src/load_torrent.cpp
	src/magnet_uri.cpp
	src/module.cpp
	src/peer_info.cpp
	src/session.cpp
	src/session_settings.cpp
	src/sha1_hash.cpp
	src/sha256_hash.cpp
	src/string.cpp
	src/torrent_handle.cpp
	src/torrent_info.cpp
	src/torrent_status.cpp
	src/utility.cpp
	src/version.cpp
)

set_target_properties(python-libtorrent
	PROPERTIES
		OUTPUT_NAME libtorrent
)

if (MSVC)
	target_compile_options(python-libtorrent PRIVATE /bigobj)
endif()

target_link_libraries(python-libtorrent
	PRIVATE
		torrent-rasterbar
		"Boost::${boost-python-module-name}"
)

# Bindings module uses deprecated libtorrent features, thus we disable these warnings
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	check_cxx_compiler_flag("-Wno-deprecated-declarations" _WNO_DEPRECATED_DECLARATIONS)
	if (_WNO_DEPRECATED_DECLARATIONS)
		target_compile_options(python-libtorrent PRIVATE -Wno-deprecated-declarations)
	endif()
endif()

if (python-install-system-dir)
	set(_PYTHON3_SITE_ARCH "${Python3_SITEARCH}")
else()
	execute_process(
		COMMAND "${Python3_EXECUTABLE}" -c [=[
import distutils.sysconfig
print(distutils.sysconfig.get_python_lib(prefix='', plat_specific=True))
]=]
		OUTPUT_VARIABLE _PYTHON3_SITE_ARCH
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
endif()

message(STATUS "Python 3 site packages: ${_PYTHON3_SITE_ARCH}")
message(STATUS "Python 3 extension suffix: ${Python3_SOABI}")

install(TARGETS python-libtorrent DESTINATION "${_PYTHON3_SITE_ARCH}")

if (python-egg-info)
	set(SETUP_PY_IN "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.cmake.in")
	set(SETUP_PY    "${CMAKE_CURRENT_BINARY_DIR}/setup.py")
	set(OUTPUT      "${CMAKE_CURRENT_BINARY_DIR}/timestamp")
	set(DEPS        python-libtorrent "${SETUP_PY}")

	configure_file(${SETUP_PY_IN} ${SETUP_PY} @ONLY)

	add_custom_command(OUTPUT ${OUTPUT}
		COMMAND ${Python3_EXECUTABLE} ${SETUP_PY} egg_info
		COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT}
		DEPENDS ${DEPS}
	)

	add_custom_target(python_bindings ALL DEPENDS ${OUTPUT})

	install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/libtorrent.egg-info" DESTINATION "${_PYTHON3_SITE_ARCH}")
endif()
