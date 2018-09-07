/*

Copyright (c) 2010-2018, Arvid Norberg
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

#ifndef TORRENT_COPY_PTR
#define TORRENT_COPY_PTR

#include <memory>

namespace libtorrent {

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
