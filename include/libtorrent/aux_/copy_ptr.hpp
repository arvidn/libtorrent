/*

Copyright (c) 2010, 2016-2020, Arvid Norberg
Copyright (c) 2017, Matthew Fioravante
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_COPY_PTR
#define TORRENT_COPY_PTR

#include <memory>

namespace lt::aux {

	template <class T>
	struct copy_ptr
	{
		copy_ptr() = default;
		explicit copy_ptr(T* t): m_ptr(t) {}
		copy_ptr(copy_ptr const& p): m_ptr(p.m_ptr ? new T(*p.m_ptr) : nullptr) {}
		copy_ptr(copy_ptr&& p) noexcept = default;

		void reset(T* t = nullptr) { m_ptr.reset(t); }
		copy_ptr& operator=(copy_ptr const& p) &
		{
			if (m_ptr == p.m_ptr) return *this;
			m_ptr.reset(p.m_ptr ? new T(*p.m_ptr) : nullptr);
			return *this;
		}
		copy_ptr& operator=(copy_ptr&& p) & noexcept = default;
		T* operator->() { return m_ptr.get(); }
		T const* operator->() const { return m_ptr.get(); }
		T& operator*() { return *m_ptr; }
		T const& operator*() const { return *m_ptr; }
		void swap(copy_ptr<T>& p) { std::swap(*this, p); }
		explicit operator bool() const { return m_ptr.get() != nullptr; }
	private:
		std::unique_ptr<T> m_ptr;
	};
}

#endif // TORRENT_COPY_PTR
