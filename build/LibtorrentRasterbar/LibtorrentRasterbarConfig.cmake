# - Config file for the libtorrent package
# It defines the LibtorrentRasterbar::torrent-rasterbar target to link against


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was LibtorrentRasterbarConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

####################################################################################

include(CMakeFindDependencyMacro)
find_dependency(Threads)
find_dependency(OpenSSL)
find_dependency(Boost)

include("${CMAKE_CURRENT_LIST_DIR}/LibtorrentRasterbarTargets.cmake")
