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
#include <queue>
#include <string>
#include <typeinfo>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

#include <boost/preprocessor/repetition/enum_params_with_a_default.hpp>
#include <boost/preprocessor/repetition/enum.hpp>
#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/repetition/enum_shifted_params.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/time.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"

#ifndef TORRENT_MAX_ALERT_TYPES
#define TORRENT_MAX_ALERT_TYPES 15
#endif

namespace libtorrent {

	class TORRENT_EXPORT alert
	{
	public:

		// only here for backwards compatibility
		enum severity_t { debug, info, warning, critical, fatal, none };

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

			all_categories = 0xffffffff
		};

		alert();
		virtual ~alert();

		// a timestamp is automatically created in the constructor
		ptime timestamp() const;

		virtual char const* what() const = 0;
		virtual std::string message() const = 0;
		virtual int category() const = 0;

#ifndef TORRENT_NO_DEPRECATE
		severity_t severity() const TORRENT_DEPRECATED { return warning; }
#endif

		virtual std::auto_ptr<alert> clone() const = 0;

	private:
		ptime m_timestamp;
	};

	class TORRENT_EXPORT alert_manager
	{
	public:
		enum { queue_size_limit_default = 1000 };

		alert_manager();
		~alert_manager();

		void post_alert(const alert& alert_);
		bool pending() const;
		std::auto_ptr<alert> get();

		template <class T>
		bool should_post() const { return (m_alert_mask & T::static_category) != 0; }

		alert const* wait_for_alert(time_duration max_wait);

		void set_alert_mask(int m) { m_alert_mask = m; }

		size_t alert_queue_size_limit() const { return m_queue_size_limit; }
		size_t set_alert_queue_size_limit(size_t queue_size_limit_);

	private:
		std::queue<alert*> m_alerts;
		mutable boost::mutex m_mutex;
		boost::condition m_condition;
		int m_alert_mask;
		size_t m_queue_size_limit;
	};

	struct TORRENT_EXPORT unhandled_alert : std::exception
	{
		unhandled_alert() {}
	};

	namespace detail {

		struct void_;

		template<class Handler
			, BOOST_PP_ENUM_PARAMS(TORRENT_MAX_ALERT_TYPES, class T)>
		void handle_alert_dispatch(
			const std::auto_ptr<alert>& alert_, const Handler& handler
			, const std::type_info& typeid_
			, BOOST_PP_ENUM_BINARY_PARAMS(TORRENT_MAX_ALERT_TYPES, T, *p))
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
			const std::auto_ptr<alert>& alert_
			, const Handler& handler
			, const std::type_info& typeid_
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

} // namespace libtorrent

#endif // TORRENT_ALERT_HPP_INCLUDED

