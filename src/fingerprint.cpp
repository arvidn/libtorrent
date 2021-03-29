/*

Copyright (c) 2016, Steven Siloti
Copyright (c) 2016-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/fingerprint.hpp"
#include "libtorrent/assert.hpp"
#include <cstring> // for strlen

namespace lt {

	namespace {

	char version_to_char(int const v)
	{
		if (v >= 0 && v < 10) return char('0' + v);
		else if (v >= 10) return char('A' + (v - 10));
		TORRENT_ASSERT_FAIL();
		return '0';
	}

	} // anonymous namespace

	std::string generate_fingerprint(std::string name, int const major
		, int const minor
		, int const revision
		, int const tag)
	{
		TORRENT_ASSERT_PRECOND(major >= 0);
		TORRENT_ASSERT_PRECOND(minor >= 0);
		TORRENT_ASSERT_PRECOND(revision >= 0);
		TORRENT_ASSERT_PRECOND(tag >= 0);
		TORRENT_ASSERT_PRECOND(name.size() == 2);
		if (name.size() < 2) name = "--";

		std::string ret;
		ret.resize(8);
		ret[0] = '-';
		ret[1] = name[0];
		ret[2] = name[1];
		ret[3] = version_to_char(major);
		ret[4] = version_to_char(minor);
		ret[5] = version_to_char(revision);
		ret[6] = version_to_char(tag);
		ret[7] = '-';
		return ret;
	}

	fingerprint::fingerprint(const char* id_string, int major, int minor
		, int revision, int tag)
		: major_version(major)
		, minor_version(minor)
		, revision_version(revision)
		, tag_version(tag)
	{
		TORRENT_ASSERT(id_string);
		TORRENT_ASSERT(major >= 0);
		TORRENT_ASSERT(minor >= 0);
		TORRENT_ASSERT(revision >= 0);
		TORRENT_ASSERT(tag >= 0);
		TORRENT_ASSERT(std::strlen(id_string) == 2);
		name[0] = id_string[0];
		name[1] = id_string[1];
	}

#if TORRENT_ABI_VERSION == 1
	std::string fingerprint::to_string() const
	{
		return generate_fingerprint(std::string(name, 2), major_version, minor_version
			, revision_version, tag_version);
	}
#endif // TORRENT_ABI_VERSION

}

