/*

Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <string>
#include "libtorrent/aux_/directory.hpp"
#include "libtorrent/aux_/path.hpp"

namespace lt::aux {

	directory::directory(std::string const& path, error_code& ec)
		: m_done(false)
	{
		ec.clear();
		std::string p{ path };

#ifdef TORRENT_WINDOWS
		// the path passed to FindFirstFile() must be
		// a pattern
		p.append((!p.empty() && p.back() != '\\') ? "\\*" : "*");
#else
		// the path passed to opendir() may not
		// end with a /
		if (!p.empty() && p.back() == '/')
			p.pop_back();
#endif

		native_path_string f = convert_to_native_path_string(p);

#ifdef TORRENT_WINDOWS
		m_handle = FindFirstFileW(f.c_str(), &m_fd);
		if (m_handle == INVALID_HANDLE_VALUE)
		{
			ec.assign(GetLastError(), system_category());
			m_done = true;
			return;
		}
#else
		m_handle = ::opendir(f.c_str());
		if (m_handle == nullptr)
		{
			ec.assign(errno, system_category());
			m_done = true;
			return;
		}
		// read the first entry
		next(ec);
#endif // TORRENT_WINDOWS
	}

	directory::~directory()
	{
#ifdef TORRENT_WINDOWS
		if (m_handle != INVALID_HANDLE_VALUE)
			FindClose(m_handle);
#else
		if (m_handle) ::closedir(m_handle);
#endif
	}

	std::string directory::file() const
	{
#ifdef TORRENT_WINDOWS
		return convert_from_native_path(m_fd.cFileName);
#else
		return convert_from_native_path(m_name.c_str());
#endif
	}

	void directory::next(error_code& ec)
	{
		ec.clear();
#ifdef TORRENT_WINDOWS
		if (FindNextFileW(m_handle, &m_fd) == 0)
		{
			m_done = true;
			int err = GetLastError();
			if (err != ERROR_NO_MORE_FILES)
				ec.assign(err, system_category());
		}
#else
		struct dirent* de;
		errno = 0;
		if ((de = ::readdir(m_handle)) != nullptr)
		{
			m_name = de->d_name;
		}
		else
		{
			if (errno) ec.assign(errno, system_category());
			m_done = true;
		}
#endif
	}

} // namespace lt::aux
