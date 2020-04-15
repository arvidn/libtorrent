# To build python bindings we need a python executable and boost python module. Unfortunately,
# their names might not be interlinked and we can not implement a general solution.
# The code below assumes default boost installation, when the module for python 2 is named
# 'python' and the module for python 3 is named 'python3'.
# To customize that one can provide a name for the Boost::python module via
# 'boost-python-module-name' variable when invoking cmake.
# E.g. on Gentoo with python 3.6 and Boost::python library name 'libboost_python-3.6.so'
# the parameter would be -Dboost-python-module-name="python-3.6".

# The extension module and the cpython executable have to use the same C runtime library. On Windows
# Python is compiled with MSVC and we will test MSVC version to make sure that it is the same for
# the given Python version and our extension module. See https://wiki.python.org/moin/WindowsCompilers
# for details. We provide a flag to skip this test for whatever reason (pass -Dskip-python-runtime-test=True)

# Sets _ret to a list of python versions (major.minor) that use the same MSVC runtime as this build does
# assumes MSVC was detected already
# See https://en.wikipedia.org/wiki/Microsoft_Visual_C++#Internal_version_numbering
function(_get_compatible_python_versions _ret)
	if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15 AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
		list(APPEND _tmp 2.6 2.7 3.0 3.1 3.2)
	elseif(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16 AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17)
		list(APPEND _tmp 3.3 3.4)
	elseif(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19 AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20)
		list(APPEND _tmp 3.5 3.6 3.7 3.8)
	endif()
	set(${_ret} ${_tmp} PARENT_SCOPE)
endfunction()


if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC" AND NOT skip-python-runtime-test)
	_get_compatible_python_versions(Python_ADDITIONAL_VERSIONS)
endif()

find_package(PythonInterp REQUIRED)
if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC" AND NOT skip-python-runtime-test)
	message(STATUS "Testing found python version. Requested: ${Python_ADDITIONAL_VERSIONS}, found: ${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}")
	if (NOT "${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}" IN_LIST Python_ADDITIONAL_VERSIONS)
		message(FATAL_ERROR "Incompatible Python and C runtime: MSVC ${CMAKE_CXX_COMPILER_VERSION} and Python ${PYTHON_VERSION_STRING}")
	endif()
endif()

set(Python_ADDITIONAL_VERSIONS "${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}")
find_package(PythonLibs REQUIRED)

if (NOT boost-python-module-name)
	# use active python
	if (PYTHON_VERSION_STRING VERSION_GREATER_EQUAL "3")
		set(_boost-python-module-name "python${PYTHON_VERSION_MAJOR}")
	else()
		set(_boost-python-module-name "python") # to overwrite possible value from a previous run
	endif()
endif()

set(boost-python-module-name ${_boost-python-module-name} CACHE STRING "Boost:python module name, e.g. 'pythom-3.6'")

find_package(Boost REQUIRED COMPONENTS ${boost-python-module-name})

python_add_module(python-libtorrent
	src/module.cpp
	src/sha1_hash.cpp
	src/converters.cpp
	src/create_torrent.cpp
	src/fingerprint.cpp
	src/utility.cpp
	src/session.cpp
	src/entry.cpp
	src/torrent_info.cpp
	src/string.cpp
	src/torrent_handle.cpp
	src/torrent_status.cpp
	src/session_settings.cpp
	src/version.cpp
	src/alert.cpp
	src/datetime.cpp
	src/peer_info.cpp
	src/ip_filter.cpp
	src/magnet_uri.cpp
	src/error_code.cpp
)

set_target_properties(python-libtorrent
	PROPERTIES
		OUTPUT_NAME libtorrent
)

target_include_directories(python-libtorrent
	PRIVATE
		${PYTHON_INCLUDE_DIRS}
)

string(TOUPPER "${boost-python-module-name}" boost_python_module_name_uppercase)
target_link_libraries(python-libtorrent
	PRIVATE
		torrent-rasterbar
		${Boost_${boost_python_module_name_uppercase}_LIBRARY}
		# Boost::python adds that but without a path to the library. Therefore we have to either
		# provide the path (but, unfortunately, FindPythonLibs.cmake does not return the library dir),
		# or give the full file name here (this FindPythonLibs.cmake provides to us).
		${PYTHON_LIBRARIES}
)

# Bindings module uses deprecated libtorrent features, thus we disable these warnings
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	check_cxx_compiler_flag("-Wno-deprecated-declarations" _WNO_DEPRECATED_DECLARATIONS)
	if (_WNO_DEPRECATED_DECLARATIONS)
		target_compile_options(python-libtorrent PRIVATE -Wno-deprecated-declarations)
	endif()
endif()

execute_process(COMMAND
  ${PYTHON_EXECUTABLE} -c "import distutils.sysconfig;
print(';'.join(map(str, [
	distutils.sysconfig.get_python_lib(plat_specific=True, prefix=''),
	distutils.sysconfig.get_config_var('EXT_SUFFIX')
])))"
	OUTPUT_VARIABLE _python_sysconfig_vars OUTPUT_STRIP_TRAILING_WHITESPACE)

list(GET _python_sysconfig_vars 0 PYTHON_SITE_PACKAGES)
list(GET _python_sysconfig_vars 1 PYTHON_EXT_SUFFIX)

message(STATUS "Python site packages: ${PYTHON_SITE_PACKAGES}")
# python 2 does not provide the 'EXT_SUFFIX' sysconfig variable, so we use cmake default then
if (NOT "${PYTHON_EXT_SUFFIX}" STREQUAL "None")
	message(STATUS "Python extension suffix: ${PYTHON_EXT_SUFFIX}")
	# we mimic the name, created by setuptools
	# example: libtorrent.cpython-36m-x86_64-linux-gnu.so
	set_target_properties(python-libtorrent PROPERTIES SUFFIX ${PYTHON_EXT_SUFFIX})
endif()

set(SETUP_PY_IN "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.cmake.in")
set(SETUP_PY    "${CMAKE_CURRENT_BINARY_DIR}/setup.py")
set(OUTPUT      "${CMAKE_CURRENT_BINARY_DIR}/timestamp")
set(DEPS        python-libtorrent "${SETUP_PY}")

configure_file(${SETUP_PY_IN} ${SETUP_PY} @ONLY)

add_custom_command(OUTPUT ${OUTPUT}
                   COMMAND ${PYTHON_EXECUTABLE} ${SETUP_PY} build -b "${CMAKE_CURRENT_SOURCE_DIR}"
                   COMMAND ${PYTHON_EXECUTABLE} ${SETUP_PY} egg_info -b "${CMAKE_CURRENT_SOURCE_DIR}"
                   COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT}
                   DEPENDS ${DEPS})

add_custom_target(python_bindings ALL DEPENDS ${OUTPUT})


install(TARGETS python-libtorrent DESTINATION "${PYTHON_SITE_PACKAGES}")
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/libtorrent.egg-info" DESTINATION "${PYTHON_SITE_PACKAGES}")
