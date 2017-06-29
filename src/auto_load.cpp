/*

Copyright (c) 2012, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "auto_load.hpp"

#include <functional>

#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"
#include "save_settings.hpp"

#include "libtorrent/aux_/path.hpp"

using namespace std::placeholders;

namespace libtorrent
{

	// TODO: get rid of these dependencies
using lt::remove;
using lt::combine_path;

namespace {

std::vector<std::string> list_dir(std::string path
	, bool (*filter_fun)(lt::string_view)
	, lt::error_code& ec)
{
	std::vector<std::string> ret;
#ifdef TORRENT_WINDOWS
	if (!path.empty() && path[path.size()-1] != '\\') path += "\\*";
	else path += "*";

	WIN32_FIND_DATAA fd;
	HANDLE handle = FindFirstFileA(path.c_str(), &fd);
	if (handle == INVALID_HANDLE_VALUE)
	{
		ec.assign(GetLastError(), boost::system::system_category());
		return ret;
	}

	do
	{
		lt::string_view p = fd.cFileName;
		if (filter_fun(p))
			ret.push_back(p.to_string());

	} while (FindNextFileA(handle, &fd));
	FindClose(handle);
#else

	if (!path.empty() && path[path.size()-1] == '/')
		path.resize(path.size()-1);

	DIR* handle = opendir(path.c_str());
	if (handle == nullptr)
	{
		ec.assign(errno, boost::system::system_category());
		return ret;
	}

	struct dirent de;
	dirent* dummy;
	while (readdir_r(handle, &de, &dummy) == 0)
	{
		if (dummy == nullptr) break;

		lt::string_view p(de.d_name);
		if (filter_fun(p))
			ret.push_back(p.to_string());
	}
	closedir(handle);
#endif
	return ret;
}
}

auto_load::auto_load(session& s, save_settings_interface* sett)
	: m_ses(s)
	, m_timer(m_ios)
	, m_settings(sett)
	, m_remove_files(true)
	, m_dir("./autoload")
	, m_scan_interval(20)
	, m_abort(false)
	, m_thread(std::bind(&auto_load::thread_fun, this))
{
	m_params_model.save_path = ".";

	if (m_settings)
	{
		int interval = m_settings->get_int("autoload_interval", -1);
		if (interval != -1) set_scan_interval(interval);
		std::string path = m_settings->get_str("autoload_dir", "");
		if (!path.empty()) set_auto_load_dir(path);
		int remove_files = m_settings->get_int("autoload_remove", -1);
		if (remove_files != -1) set_remove_files(remove_files);
		m_params_model.save_path = m_settings->get_str("save_path", ".");
	}
}

auto_load::~auto_load()
{
	std::unique_lock<std::mutex> l(m_mutex);
	m_abort = true;
	l.unlock();
	m_timer.cancel();
	m_thread.join();
}

void auto_load::set_remove_files(bool r)
{
	std::unique_lock<std::mutex> l(m_mutex);
	m_remove_files = r;
	if (m_settings) m_settings->set_int("autoload_remove", r);
}

bool auto_load::remove_files() const
{
	std::unique_lock<std::mutex> l(m_mutex);
	return m_remove_files;
}

void auto_load::set_params_model(add_torrent_params const& p)
{
	std::unique_lock<std::mutex> l(m_mutex);
	m_params_model = p;
}

add_torrent_params auto_load::params_model() const
{
	std::unique_lock<std::mutex> l(m_mutex);
	return m_params_model;
}

void auto_load::set_auto_load_dir(std::string const& dir)
{
	std::unique_lock<std::mutex> l(m_mutex);
	m_dir = dir;
	if (m_settings) m_settings->set_str("autoload_dir", dir);
	l.unlock();

	// reset the timeout to use the new interval
	error_code ec;
	m_timer.expires_from_now(seconds(0), ec);
	m_timer.async_wait(std::bind(&auto_load::on_scan, this, _1));
}

void auto_load::set_scan_interval(int s)
{
	std::unique_lock<std::mutex> l(m_mutex);
	if (m_scan_interval == s) return;
	m_scan_interval = s;
	if (m_settings) m_settings->set_int("autoload_interval", s);
	l.unlock();

	// interval of 0 means disabled
	if (m_scan_interval == 0)
	{
		error_code ec;
		m_timer.cancel(ec);
		return;
	}

	// reset the timeout to use the new interval
	error_code ec;
	m_timer.expires_from_now(seconds(m_scan_interval), ec);
	m_timer.async_wait(std::bind(&auto_load::on_scan, this, _1));
}

void auto_load::thread_fun()
{
	// the std::mutex must be held while inspecting m_abort
	std::unique_lock<std::mutex> l(m_mutex);

	error_code ec;
	m_timer.expires_from_now(seconds(1), ec);
	m_timer.async_wait(std::bind(&auto_load::on_scan, this, _1));

	while (!m_abort)
	{
		l.unlock();
		m_ios.reset();
		error_code ec;
		m_ios.run(ec);
		l.lock();
	}
}

void auto_load::on_scan(error_code const& e)
{
	if (e) return;
	std::unique_lock<std::mutex> l(m_mutex);
	if (m_abort) return;

	// interval of 0 means disabled
	if (m_scan_interval == 0) return;
	
	std::string path = m_dir;
	bool remove_files = m_remove_files;
	l.unlock();

	error_code ec;
	std::vector<std::string> ents = list_dir(path
		, [](lt::string_view p) { return p.size() > 8 && p.substr(p.size() - 8) == ".torrent"; }, ec);
	for (auto const& file : ents)
	{
		if (m_already_loaded.count(file))
		{
			if (remove_files)
			{
				std::string file_path = combine_path(path, file);
				remove(file_path, ec);
				if (!ec) m_already_loaded.erase(m_already_loaded.find(file));
			}
			continue;
		}

		error_code tec;
		std::string file_path = combine_path(path, file);
		auto ti = std::make_shared<torrent_info>(file_path, std::ref(tec));

		// assume the file isn't fully written yet.
		if (tec) continue;

		l.lock();
		add_torrent_params p = m_params_model;
		l.unlock();

		p.ti = ti;
		m_ses.async_add_torrent(p);

		// TODO: there should be a configuration option to
		// move the torrent file into a different directory
		if (remove_files)
			remove(file_path, ec);
		else
			m_already_loaded.insert(file);
	}

	l.lock();
	int interval = m_scan_interval;
	l.unlock();

	// interval of 0 means disabled
	if (interval == 0) return;

	m_timer.expires_from_now(seconds(interval), ec);
	m_timer.async_wait(std::bind(&auto_load::on_scan, this, _1));
}

}

