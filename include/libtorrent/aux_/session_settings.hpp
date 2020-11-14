/*

Copyright (c) 2014, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, Steven Siloti
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

#ifndef TORRENT_AUX_SESSION_SETTINGS_HPP_INCLUDED
#define TORRENT_AUX_SESSION_SETTINGS_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/assert.hpp"

#include <string>
#include <array>
#include <bitset>
#include <mutex>
#include <functional>

namespace libtorrent {
namespace aux {

	struct TORRENT_EXTRA_EXPORT session_settings_single_thread
	{
		void set_str(int name, std::string value)
		{ set<std::string>(m_strings, name, std::move(value), settings_pack::string_type_base); }
		void set_int(int name, int value)
		{ set<int>(m_ints, name, value, settings_pack::int_type_base); }
		void set_bool(int name, bool value)
		{ set<bool>(m_bools, name, value, settings_pack::bool_type_base); }

		std::string const& get_str(int name) const
		{ return get<std::string const&>(m_strings, name, settings_pack::string_type_base); }
		int get_int(int name) const
		{ return get<int>(m_ints, name, settings_pack::int_type_base); }
		bool get_bool(int name) const
		{ return get<bool>(m_bools, name, settings_pack::bool_type_base); }

		session_settings_single_thread();

	private:

		template <typename T, typename Container>
		void set(Container& c, int const name, T val
			, int const type)
		{
			TORRENT_ASSERT((name & settings_pack::type_mask) == type);
			if ((name & settings_pack::type_mask) != type) return;
			size_t const index = name & settings_pack::index_mask;
			TORRENT_ASSERT(index < c.size());
			c[index] = std::move(val);
		}

		template <typename T, typename Container>
		T get(Container const& c, int const name, int const type) const
		{
			static typename std::remove_reference<T>::type empty;
			TORRENT_ASSERT((name & settings_pack::type_mask) == type);
			if ((name & settings_pack::type_mask) != type) return empty;
			size_t const index = name & settings_pack::index_mask;
			TORRENT_ASSERT(index < c.size());
			return c[index];
		}

		std::array<std::string, settings_pack::num_string_settings> m_strings;
		std::array<int, settings_pack::num_int_settings> m_ints;
		std::bitset<settings_pack::num_bool_settings> m_bools;
	};

	struct TORRENT_EXTRA_EXPORT session_settings final : settings_interface
	{
		void set_str(int name, std::string value) override
		{
			std::unique_lock<std::mutex> l(m_mutex);
			return m_store.set_str(name, std::move(value));
		}
		void set_int(int name, int value) override
		{
			std::unique_lock<std::mutex> l(m_mutex);
			m_store.set_int(name, value);
		}
		void set_bool(int name, bool value) override
		{
			std::unique_lock<std::mutex> l(m_mutex);
			m_store.set_bool(name, value);
		}

		std::string const& get_str(int name) const override
		{
			std::unique_lock<std::mutex> l(m_mutex);
			return m_store.get_str(name);
		}
		int get_int(int name) const override
		{
			std::unique_lock<std::mutex> l(m_mutex);
			return m_store.get_int(name);
		}
		bool get_bool(int name) const override
		{
			std::unique_lock<std::mutex> l(m_mutex);
			return m_store.get_bool(name);
		}

		bool has_val(int) const override { return true; }

		session_settings();
		explicit session_settings(settings_pack const&);

		void bulk_set(std::function<void(session_settings_single_thread&)>);
		void bulk_get(std::function<void(session_settings_single_thread const&)>) const;

		// since std::mutex is not copyable, we have to explicitly just copy the
		// underlying storage object. Lock the object we're copying from first,
		// and forward to a private copy constructor to keep the lock alive
		// inspired by https://www.justsoftwaresolutions.co.uk/threading/thread-safe-copy-constructors.html
		session_settings(session_settings const& lhs)
			: session_settings(lhs, std::unique_lock<std::mutex>(lhs.m_mutex))
		{}
		session_settings(session_settings&& lhs)
			: session_settings(std::move(lhs), std::unique_lock<std::mutex>(lhs.m_mutex))
		{}

		session_settings& operator=(session_settings const& rhs)
		{
			if (this == &rhs) return *this;
			// in C++17, use a single std::scoped_lock instead
			std::lock(rhs.m_mutex, m_mutex);
			std::unique_lock<std::mutex> l1(rhs.m_mutex, std::adopt_lock);
			std::unique_lock<std::mutex> l2(m_mutex, std::adopt_lock);
			m_store = rhs.m_store;
			return *this;
		}
		session_settings& operator=(session_settings&& rhs)
		{
			if (this == &rhs) return *this;
			m_store = std::move(rhs.m_store);
			return *this;
		}

	private:

		session_settings(session_settings const& lhs, std::unique_lock<std::mutex> const&)
			: settings_interface(lhs)
			, m_store(lhs.m_store)
		{}

		session_settings(session_settings&& lhs, std::unique_lock<std::mutex> const&)
			: settings_interface(lhs)
			, m_store(std::move(lhs.m_store))
		{}

		session_settings_single_thread m_store;
		mutable std::mutex m_mutex;
	};

}
}

namespace libtorrent {
	TORRENT_EXTRA_EXPORT void initialize_default_settings(aux::session_settings_single_thread& s);
}

#endif
