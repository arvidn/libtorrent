/*

Copyright (c) 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_CLIENT_DATA_HPP_INCLUDED
#define TORRENT_CLIENT_DATA_HPP_INCLUDED

#include "libtorrent/aux_/export.hpp"
#include <type_traits>

namespace lt {

// A thin wrapper around a void pointer used as "user data". i.e. an opaque
// cookie passed in to libtorrent and returned on demand. It adds type-safety by
// requiring the same type be requested out of it as was assigned to it.
struct TORRENT_EXPORT client_data_t
{
	// construct a nullptr client data
	client_data_t() = default;

	// initialize the client data with the specified pointer
	template <typename T>
	explicit client_data_t(T* v)
		: m_type_ptr(type<T>())
		, m_client_ptr(v)
	{}

	// assigns a new pointer to the client data
	template <typename T>
	client_data_t& operator=(T* v)
	{
		m_type_ptr = type<T>();
		m_client_ptr = v;
		return *this;
	}

	// request to retrieve the pointer back again. The type ``T`` must be
	// identical to the type of the pointer assigned earlier, including
	// cv-qualifiers.
	template <typename T>
	T* get() const
	{
		if (m_type_ptr != type<T>()) return nullptr;
		return static_cast<T*>(m_client_ptr);
	}
	template <typename T, typename U = typename std::enable_if<std::is_pointer<T>::value>::type>
	explicit operator T() const
	{
		if (m_type_ptr != type<typename std::remove_pointer<T>::type>()) return nullptr;
		return static_cast<T>(m_client_ptr);
	}

#if TORRENT_ABI_VERSION > 2
	// we don't allow type-unsafe operations
	operator void*() const = delete;
	operator void const*() const = delete;
	client_data_t& operator=(void*) = delete;
	client_data_t& operator=(void const*) = delete;
#endif

private:
	template <typename T>
	char const* type() const
	{
		// each unique T will instantiate a unique "instance", and have a unique
		// address
		static const char instance = 0;
		return &instance;
	}
	char const* m_type_ptr = nullptr;
	void* m_client_ptr = nullptr;
};

}

#endif
