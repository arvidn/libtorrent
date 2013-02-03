/*

Copyright (c) 2003, Arvid Norberg, Daniel Wallin
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

#ifndef TORRENT_ALERT_HPP_INCLUDED
#define TORRENT_ALERT_HPP_INCLUDED

#include <memory>
#include <deque>
#include <string>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/function/function1.hpp>

#include <boost/preprocessor/repetition/enum_params_with_a_default.hpp>
#include <boost/preprocessor/repetition/enum.hpp>
#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/repetition/enum_shifted_params.hpp>
#include <boost/preprocessor/repetition/enum_shifted_binary_params.hpp>

#ifndef TORRENT_DISABLE_EXTENSIONS
#include <boost/shared_ptr.hpp>
#include <list>
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/ptime.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/io_service_fwd.hpp"

#ifndef TORRENT_MAX_ALERT_TYPES
#define TORRENT_MAX_ALERT_TYPES 15
#endif

namespace libtorrent {

#ifndef TORRENT_DISABLE_EXTENSIONS
	struct plugin;
#endif

	class TORRENT_EXPORT alert
	{
	public:

#ifndef TORRENT_NO_DEPRECATE
		// only here for backwards compatibility
		enum severity_t { debug, info, warning, critical, fatal, none };
#endif

		enum category_t
		{
			error_notification = 0x1,
			peer_notification = 0x2,
			port_mapping_notification = 0x4,
			storage_notification = 0x8,
			tracker_notification = 0x10,
			debug_notification = 0x20,
			status_notification = 0x40,
			progress_notification = 0x80,
			ip_block_notification = 0x100,
			performance_warning = 0x200,
			dht_notification = 0x400,
			stats_notification = 0x800,
			rss_notification = 0x1000,

			// since the enum is signed, make sure this isn't
			// interpreted as -1. For instance, boost.python
			// does that and fails when assigning it to an
			// unsigned parameter.
			all_categories = 0x7fffffff
		};

		alert();
		virtual ~alert();

		// a timestamp is automatically created in the constructor
		ptime timestamp() const;

		virtual int type() const = 0;
		virtual char const* what() const = 0;
		virtual std::string message() const = 0;
		virtual int category() const = 0;
		virtual bool discardable() const { return true; }

#ifndef TORRENT_NO_DEPRECATE
		TORRENT_DEPRECATED_PREFIX
		severity_t severity() const TORRENT_DEPRECATED { return warning; }
#endif

		virtual std::auto_ptr<alert> clone() const = 0;

	private:
		ptime m_timestamp;
	};

	class TORRENT_EXTRA_EXPORT alert_manager
	{
	public:
		alert_manager(io_service& ios, int queue_limit
			, boost::uint32_t alert_mask = alert::error_notification);
		~alert_manager();

		void post_alert(const alert& alert_);
		void post_alert_ptr(alert* alert_);
		bool pending() const;
		std::auto_ptr<alert> get();
		void get_all(std::deque<alert*>* alerts);

		template <class T>
		bool should_post() const
		{
			mutex::scoped_lock lock(m_mutex);
			if (m_alerts.size() >= m_queue_size_limit) return false;
			return (m_alert_mask & T::static_category) != 0;
		}

		alert const* wait_for_alert(time_duration max_wait);

		void set_alert_mask(boost::uint32_t m)
		{
			mutex::scoped_lock lock(m_mutex);
			m_alert_mask = m;
		}

		int alert_mask() const { return m_alert_mask; }

		size_t alert_queue_size_limit() const { return m_queue_size_limit; }
		size_t set_alert_queue_size_limit(size_t queue_size_limit_);

		void set_dispatch_function(boost::function<void(std::auto_ptr<alert>)> const&);

#ifndef TORRENT_DISABLE_EXTENSIONS
		void add_extension(boost::shared_ptr<plugin> ext);
#endif

	private:
		void post_impl(std::auto_ptr<alert>& alert_);

		std::deque<alert*> m_alerts;
		mutable mutex m_mutex;
//		event m_condition;
		boost::uint32_t m_alert_mask;
		size_t m_queue_size_limit;
		boost::function<void(std::auto_ptr<alert>)> m_dispatch;
		io_service& m_ios;

#ifndef TORRENT_DISABLE_EXTENSIONS
		typedef std::list<boost::shared_ptr<plugin> > ses_extension_list_t;
		ses_extension_list_t m_ses_extensions;
#endif
	};

	struct TORRENT_EXPORT unhandled_alert : std::exception
	{
		unhandled_alert() {}
	};

#ifndef BOOST_NO_TYPEID

	namespace detail {

		struct void_;

		template<class Handler
			, BOOST_PP_ENUM_PARAMS(TORRENT_MAX_ALERT_TYPES, class T)>
		void handle_alert_dispatch(
			const std::auto_ptr<alert>& alert_, const Handler& handler
			, const std::type_info& typeid_
			, T0*, BOOST_PP_ENUM_SHIFTED_BINARY_PARAMS(TORRENT_MAX_ALERT_TYPES, T, *p))
		{
			if (typeid_ == typeid(T0))
				handler(*static_cast<T0*>(alert_.get()));
			else
				handle_alert_dispatch(alert_, handler, typeid_
					, BOOST_PP_ENUM_SHIFTED_PARAMS(
					TORRENT_MAX_ALERT_TYPES, p), (void_*)0);
		}

		template<class Handler>
		void handle_alert_dispatch(
			const std::auto_ptr<alert>&
			, const Handler&
			, const std::type_info&
			, BOOST_PP_ENUM_PARAMS(TORRENT_MAX_ALERT_TYPES, void_* BOOST_PP_INTERCEPT))
		{
			throw unhandled_alert();
		}

	} // namespace detail

	template<BOOST_PP_ENUM_PARAMS_WITH_A_DEFAULT(
		TORRENT_MAX_ALERT_TYPES, class T, detail::void_)>
	struct TORRENT_EXPORT handle_alert
	{
		template<class Handler>
		handle_alert(const std::auto_ptr<alert>& alert_
			, const Handler& handler)
		{
			#define ALERT_POINTER_TYPE(z, n, text) (BOOST_PP_CAT(T, n)*)0

			detail::handle_alert_dispatch(alert_, handler, typeid(*alert_)
				, BOOST_PP_ENUM(TORRENT_MAX_ALERT_TYPES, ALERT_POINTER_TYPE, _));

			#undef ALERT_POINTER_TYPE
		}
	};

#endif // BOOST_NO_TYPEID

template <class T>
T* alert_cast(alert* a)
{
	if (a == 0) return 0;
	if (a->type() == T::alert_type) return static_cast<T*>(a);
	return 0;
}

template <class T>
T const* alert_cast(alert const* a)
{
	if (a == 0) return 0;
	if (a->type() == T::alert_type) return static_cast<T const*>(a);
	return 0;
}

} // namespace libtorrent

#endif // TORRENT_ALERT_HPP_INCLUDED

