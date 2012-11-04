/*

Copyright (c) 2012, Arvid Norberg, Magnus Jonsson
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

#include <boost/bind.hpp>
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"

namespace libtorrent
{

auto_load::auto_load(session& s)
	: m_ses(s)
	, m_timer(m_ios)
	, m_dir("./auto_load")
	, m_scan_interval(20)
	, m_abort(false)
	, m_thread(boost::bind(&auto_load::thread_fun, this))
{
	m_params_model.save_path = ".";
	error_code ec;
	m_timer.expires_from_now(seconds(0), ec);
	m_timer.async_wait(boost::bind(&auto_load::on_scan, this, _1));
}

auto_load::~auto_load()
{
	mutex::scoped_lock l(m_mutex);
	m_abort = true;
	l.unlock();
	m_timer.cancel();
	m_thread.join();
}

void auto_load::set_auto_load_dir(std::string const& dir)
{
	mutex::scoped_lock l(m_mutex);
	m_dir = dir;
	l.unlock();
	
	// reset the timeout to use the new interval
	error_code ec;
	m_timer.expires_from_now(seconds(0), ec);
}

void auto_load::set_scan_interval(int s)
{
	mutex::scoped_lock l(m_mutex);
	if (m_scan_interval == s) return;
	m_scan_interval = s;
	l.unlock();

	// reset the timeout to use the new interval
	error_code ec;
	m_timer.expires_from_now(seconds(m_scan_interval), ec);
}

void auto_load::thread_fun()
{
	// the mutex must be held while inspecting m_abort
	mutex::scoped_lock l(m_mutex);
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
	mutex::scoped_lock l(m_mutex);
	if (m_abort) return;
	
	std::string path = m_dir;
	l.unlock();

	error_code ec;
	for (directory dir(path, ec); !ec && !dir.done(); dir.next(ec))
	{
		if (extension(dir.file()) != ".torrent") continue;

		error_code tec;
		std::string file_path = combine_path(path, dir.file());
		boost::intrusive_ptr<torrent_info> ti(new torrent_info(file_path), tec);

		// assume the file isn't fully written yet.
		if (tec) continue;

		l.lock();
		add_torrent_params p = m_params_model;
		l.unlock();

		p.ti = ti;
		m_ses.async_add_torrent(p);

		// TODO: there should be a configuration option to
		// instead move the torrent file into a different directory
		remove(file_path, ec);
	}

	l.lock();
	int interval = m_scan_interval;
	l.unlock();

	m_timer.expires_from_now(seconds(interval), ec);
	m_timer.async_wait(boost::bind(&auto_load::on_scan, this, _1));
}

}

