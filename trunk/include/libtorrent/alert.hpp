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

#ifndef TORRENT_ERROR_HPP_INCLUDED
#define TORRENT_ERROR_HPP_INCLUDED

#include <memory>
#include <queue>
#include <string>
#include <cassert>
#include <typeinfo>

#include <boost/thread/mutex.hpp>

#include <boost/preprocessor/repetition/enum_params_with_a_default.hpp>
#include <boost/preprocessor/repetition/enum.hpp>
#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/repetition/enum_shifted_params.hpp>

namespace libtorrent {

	class alert
	{
	public:
		enum severity_t { debug, info, warning, critital, fatal, none };

		alert(severity_t severity, const std::string& msg)
			: m_msg(msg)
			, m_severity(severity)
		{}

		virtual ~alert() {}

		const std::string& msg() const
		{
			return m_msg;
		}

		severity_t severity() const
		{
			return m_severity;
		}

		virtual std::auto_ptr<alert> clone() const = 0;

	private:
		std::string m_msg;
		severity_t m_severity;
	};

	class alert_manager
	{
	public:
		alert_manager();
		~alert_manager();

		void post_alert(const alert& alert_);
		bool pending() const;
		std::auto_ptr<alert> get();

		void set_severity(alert::severity_t severity);
		bool should_post(alert::severity_t severity) const
		{ return severity >= m_severity; }

	private:
		std::queue<alert*> m_alerts;
		alert::severity_t m_severity;
		mutable boost::mutex m_mutex;
	};

	struct unhandled_alert : std::exception
	{
		unhandled_alert() {}
	};

	namespace detail {

		struct void_;

		template<
			class Handler
		  , BOOST_PP_ENUM_PARAMS(5, class T)
		  >
		void handle_alert_dispatch(
				const std::auto_ptr<alert>& alert_
			  , const Handler& handler
			  , const std::type_info& typeid_
			  , BOOST_PP_ENUM_BINARY_PARAMS(5, T, *p))
		{
			if (typeid_ == typeid(T0))
				handler(*static_cast<T0*>(alert_.get()));
			else
				handle_alert_dispatch(
					alert_
				  , handler
				  , typeid_
				  , BOOST_PP_ENUM_SHIFTED_PARAMS(5, p), (void_*)0
				);
		}

		template<class Handler>
		void handle_alert_dispatch(
				const std::auto_ptr<alert>& alert_
			  , const Handler& handler
			  , const std::type_info& typeid_
			  , BOOST_PP_ENUM_PARAMS(5, void_* BOOST_PP_INTERCEPT))
		{
			throw unhandled_alert();
		}

	} // namespace detail

	template<
	  	BOOST_PP_ENUM_PARAMS_WITH_A_DEFAULT(5, class T, detail::void_)
	  >
	struct handle_alert
	{
		template<class Handler>
		handle_alert(
			const std::auto_ptr<alert>& alert_
		  , const Handler& handler)
		{
			#define ALERT_POINTER_TYPE(z, n, text) (BOOST_PP_CAT(T, n)*)0

			detail::handle_alert_dispatch(
				alert_
			  , handler
			  , typeid(*alert_)
			  , BOOST_PP_ENUM(5, ALERT_POINTER_TYPE, _)
			);

			#undef ALERT_POINTER_TYPE
		}
	};

} // namespace libtorrent

#endif // TORRENT_ERROR_HPP_INCLUDED

