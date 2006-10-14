============================================
libtorrent setup for VS.NET 2005 Development
============================================

:Author: THOMAS GODDARD
:Contact: www.thomasgoddard.com

Prerequisites
-------------

1. Download boost from boost.org
2. Download libtorrent
3. Extract both to a folder called libtorrent

Compiling boost for VS.NET 2005
-------------------------------

1. Copy bjam.exe to libtorrent\\boost_[version]\\tools\\build
2. Add the path "libtorrent\\boost_[version]\\tools\\build" to the windows path environment variable
3. Log off / log back on
4. Open the file "libtorrent\\boost_[version]\\tools\\build\\user-config.jam" in notepad
5. Uncomment the ``# using msvc;`` line by removing the #
6. Replace the line with: ``using vc-8_0 ;``
7. Save the file and close it
8. Open the visual studio .net command line in the VS.net 2005 folder in your start menu
9. CD to the "libtorrent\\boost_[version]" folder
10. Type: ``bjam "-sTOOLS=vc-8_0" install`` and hit enter
11. Be patient and wait for it to finish

Troubleshooting
...............

* Make sure to CD to the root of the boost directory before running the install.

* For additional details and information on using Visual Studio 2005 Express addition, see the Boost Getting Started Guide.

Setting up and compiling libtorrent with VS.NET 2005
-----------------------------------------------------

1. Create a new vs.net 2005 MFC project and solution file in the root of the libtorrent folder where you extracted all of the libtorrent files.
2. Include the files contained in the src folder, excluding the makefiles.
   **NOTE:**
   Only include either file.cpp or file_win.cpp.  Read here to determine which file to include:
   http://www.rasterbar.com/products/libtorrent/docs.html#building-with-other-build-systems

3. Include all .c files in the zlib folder.

4. Add the following Additional Include Header Files::

     "libtorrent\include"
     "C:\Boost\include\boost-1_33_1"
     "libtorrent\zlib"
     "libtorrent\include\libtorrent"

5. Add the following Preprocessor Definitions::

     WIN32
     WIN32_LEAN_AND_MEAN
     _WIN32_WINNT=0x0500
     BOOST_ALL_NO_LIB
     _FILE_OFFSET_BITS=64
     BOOST_THREAD_USE_LIB
     TORRENT_BUILDING_SHARED
     TORRENT_LINKING_SHARED
     UNICODE

6. Add ``C:\Boost\lib`` to Additional Library Directories

7. Add the following Additional Dependencies::

     wsock32.lib
     libboost_thread-vc80-mt.lib
     libboost_filesystem-vc80-mt.lib
     libboost_date_time-vc80-mt.lib

8.  Set the Runtime Library to Multi-threaded Debug DLL (/MDd) under the code generation section.


Troubleshooting
...............

Error: error LNK2005:already defined etc...
	Make sure you use the Multi-threaded Debug DLL (/MDd)

Error: error linking zlib related files...
	Make sure to include all .c files in the zlib folder.

Runtime error in client_test.exe
	If you're using boost-1.33.1, there is a bug in the program options
	library which will make VS.NET 2005 assert. For a patch, see:
	http://thread.gmane.org/gmane.comp.lib.boost.devel/140932/focus=140932

