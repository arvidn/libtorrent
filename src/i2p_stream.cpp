/*

Copyright (c) 2015, Mikhail Titov
Copyright (c) 2017, Andrei Kurushin
Copyright (c) 2009, 2011, 2013-2020, Arvid Norberg
Copyright (c) 2016, 2019, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"

#if TORRENT_USE_I2P

#include "libtorrent/i2p_stream.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/settings_pack.hpp"

namespace lt {

	struct i2p_error_category final : boost::system::error_category
	{
		const char* name() const BOOST_SYSTEM_NOEXCEPT override
		{ return "i2p error"; }
		std::string message(int ev) const override
		{
			static char const* messages[] =
			{
				"no error",
				"parse failed",
				"cannot reach peer",
				"i2p error",
				"invalid key",
				"invalid id",
				"timeout",
				"key not found",
				"duplicated id"
			};

			if (ev < 0 || ev >= i2p_error::num_errors) return "unknown error";
			return messages[ev];
		}
		boost::system::error_condition default_error_condition(
			int ev) const BOOST_SYSTEM_NOEXCEPT override
		{ return {ev, *this}; }
	};


	boost::system::error_category& i2p_category()
	{
		static i2p_error_category i2p_category;
		return i2p_category;
	}

	namespace i2p_error
	{
		boost::system::error_code make_error_code(i2p_error_code e)
		{
			return {e, i2p_category()};
		}
	}

	i2p_connection::i2p_connection(io_context& ios)
		: m_port(0)
		, m_state(sam_idle)
		, m_io_service(ios)
	{}

	i2p_connection::~i2p_connection() = default;

	void i2p_connection::close(error_code& e)
	{
		if (m_sam_socket) m_sam_socket->close(e);
	}

	aux::proxy_settings i2p_connection::proxy() const
	{
		aux::proxy_settings ret;
		ret.hostname = m_hostname;
		ret.port = std::uint16_t(m_port);
		ret.type = settings_pack::i2p_proxy;
		return ret;
	}

	i2p_stream::i2p_stream(io_context& io_context)
		: proxy_base(io_context)
		, m_id(nullptr)
		, m_command(cmd_create_session)
		, m_state(read_hello_response)
	{
#if TORRENT_USE_ASSERTS
		m_magic = 0x1337;
#endif
	}

#if TORRENT_USE_ASSERTS
	i2p_stream::~i2p_stream()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_magic = 0;
	}
#endif

	static_assert(std::is_nothrow_move_constructible<i2p_stream>::value
		, "should be nothrow move constructible");
}

#endif
