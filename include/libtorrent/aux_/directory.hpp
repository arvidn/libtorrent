/*

Copyright (c) 2016-2017, Steven Siloti
Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DIRECTORY_HPP_INCLUDED
#define TORRENT_DIRECTORY_HPP_INCLUDED

#include <string>
#include "libtorrent/error_code.hpp"

#ifdef TORRENT_WINDOWS
// windows part
#include "libtorrent/aux_/windows.hpp"
#include <winioctl.h>
#include <sys/types.h>
#else
// posix part

#include <dirent.h> // for DIR

#endif
namespace lt::aux {

struct TORRENT_EXTRA_EXPORT directory
{
	directory(std::string const& path, error_code& ec);
	~directory();

	directory(directory const&) = delete;
	directory& operator=(directory const&) = delete;

	void next(error_code& ec);
	std::string file() const;
	bool done() const { return m_done; }
private:
#ifdef TORRENT_WINDOWS
	HANDLE m_handle;
	WIN32_FIND_DATAW m_fd;
#else
	DIR* m_handle;
	std::string m_name;
#endif
	bool m_done;
};

}
#endif
