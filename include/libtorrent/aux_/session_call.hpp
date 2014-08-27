/*

Copyright (c) 2014, Arvid Norberg, Steven Siloti
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

#ifndef TORRENT_SESSION_CALL_HPP_INCLUDED
#define TORRENT_SESSION_CALL_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/aux_/session_impl.hpp"

#include <boost/function.hpp>

namespace libtorrent { namespace aux {

void blocking_call();
void dump_call_profile();

void fun_wrap(bool& done, condition_variable& e, mutex& m, boost::function<void(void)> f);

template <class R>
void fun_ret(R& ret, bool& done, condition_variable& e, mutex& m, boost::function<R(void)> f)
{
	ret = f();
	mutex::scoped_lock l(m);
	done = true;
	e.notify_all();
}

void torrent_wait(bool& done, aux::session_impl& ses);

void sync_call(aux::session_impl& ses, boost::function<void(void)> f);

template <typename Handle>
void sync_call_handle(Handle& h, boost::function<void(void)> f)
{
	bool done = false;
	session_impl& ses = (session_impl&) h->session();
	ses.m_io_service.dispatch(boost::bind(&aux::fun_wrap
		, boost::ref(done)
		, boost::ref(ses.cond)
		, boost::ref(ses.mut), f));
	h.reset();
	aux::torrent_wait(done, ses);
}

template <typename Ret>
Ret sync_call_ret(aux::session_impl& ses, boost::function<Ret(void)> f)
{
	bool done = false;
	Ret r;
	ses.m_io_service.dispatch(boost::bind(&fun_ret<Ret>
		, boost::ref(r)
		, boost::ref(done)
		, boost::ref(ses.cond)
		, boost::ref(ses.mut)
		, f));
	torrent_wait(done, ses);
	return r;
}

template <typename Handle, typename Ret>
void sync_call_ret_handle(Handle& h, Ret& r, boost::function<Ret(void)> f)
{
	bool done = false;
	session_impl& ses = (session_impl&) h->session();
	ses.m_io_service.dispatch(boost::bind(&aux::fun_ret<Ret>
		, boost::ref(r)
		, boost::ref(done)
		, boost::ref(ses.cond)
		, boost::ref(ses.mut)
		, f));
	h.reset();
	aux::torrent_wait(done, ses);
}

} } // namespace aux namespace libtorrent

#endif // TORRENT_SESSION_CALL_HPP_INCLUDED
