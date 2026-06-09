/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_IO_TEST_HPP
#define TORRENT_DISK_IO_TEST_HPP

#include "libtorrent/config.hpp"
#include "libtorrent/session_params.hpp" // for disk_io_constructor_type
#include "libtorrent/posix_disk_io.hpp"
#include "libtorrent/pread_disk_io.hpp"

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
#include "libtorrent/mmap_disk_io.hpp"
#endif

#include "test.hpp"

#include <boost/preprocessor/cat.hpp>

// indirection layer so the BOOST_PP_CAT argument is expanded before
// TORRENT_TEST stringifies the test name (otherwise the registered test
// names would come out as e.g. "BOOST_PP_CAT(checking, _mmap)").
#define TORRENT_TEST_DISK_IO_REGISTER_(name) TORRENT_TEST(name)

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
#define TORRENT_TEST_DISK_IO_MMAP_(test_name) \
	TORRENT_TEST_DISK_IO_REGISTER_(BOOST_PP_CAT(test_name, _mmap)) \
	{ \
		BOOST_PP_CAT(disk_io_test_, test_name)(lt::mmap_disk_io_constructor); \
	}
#else
#define TORRENT_TEST_DISK_IO_MMAP_(test_name)
#endif

// Registers one test per disk I/O backend (mmap_disk_io_constructor where
// available, posix_disk_io_constructor, and pread_disk_io_constructor). The
// body sees `disk_io` as a `lt::disk_io_constructor_type`, which can be passed
// to session_params (`sp.disk_io_constructor = disk_io;`) or any helper that
// creates a session.
//
// Example:
//
//   TORRENT_TEST_DISK_IO(checking_v2)
//   {
//       test_checking(v2, disk_io);
//   }
//
// expands to `checking_v2_mmap`, `checking_v2_posix`, and `checking_v2_pread`
// test cases. Each is registered and reported individually so a
// backend-specific failure is obvious from the name.
#define TORRENT_TEST_DISK_IO(test_name) \
	static void BOOST_PP_CAT(disk_io_test_, test_name)(lt::disk_io_constructor_type disk_io); \
	TORRENT_TEST_DISK_IO_MMAP_(test_name) \
	TORRENT_TEST_DISK_IO_REGISTER_(BOOST_PP_CAT(test_name, _posix)) \
	{ \
		BOOST_PP_CAT(disk_io_test_, test_name)(lt::posix_disk_io_constructor); \
	} \
	TORRENT_TEST_DISK_IO_REGISTER_(BOOST_PP_CAT(test_name, _pread)) \
	{ \
		BOOST_PP_CAT(disk_io_test_, test_name)(lt::pread_disk_io_constructor); \
	} \
	static void BOOST_PP_CAT(disk_io_test_, test_name)(lt::disk_io_constructor_type disk_io)

#endif // TORRENT_DISK_IO_TEST_HPP
