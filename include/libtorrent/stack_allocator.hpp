/*

Copyright (c) 2015-2016, Arvid Norberg
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

#ifndef TORRENT_STACK_ALLOCATOR

#include "libtorrent/assert.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/thread.hpp"

namespace libtorrent { namespace aux
{
	struct stack_allocator
	{
		// This only prevents the allocator from being reset()
		// while it is locked. Other threads can still lock the
		// it, but only the state of the first lock is saved.
		// Once the lock count reaches 0 it will be restored back
		// to the sate before the first lock, or if a reset() was
		// issued while the allocator was locked then it will be
		// reset.
		struct stack_allocator_lock
		{
			friend class stack_allocator;

			stack_allocator_lock(stack_allocator& alloc) : m_alloc(alloc)
			{
				lock();
			}

			stack_allocator& allocator() const
			{
				return m_alloc;
			}

			void lock()
			{
				mutex::scoped_lock lock(m_alloc.m_mutex);

				TORRENT_ASSERT(!m_locked);

				m_alloc.m_consec_locks++;

				while (m_alloc.m_consec_locks > 100)
				{
					// if the allocator is kept locked for too long
					// we need to block until all references are released
					// or the allocator may grow indefinitely. The user
					// should take care to avoid this.
					lock.unlock();
					std::this_thread::yield();
					lock.lock();
				}

				if (m_alloc.m_locks++ == 0)
				{
					// we only save the state for the first lock
					TORRENT_ASSERT(m_alloc.m_saved_size == -1);
					m_alloc.m_saved_size = m_alloc.m_storage.size();
				}
				m_locked = true;
			}

			void unlock(const bool reset = false)
			{
				mutex::scoped_lock lock(m_alloc.m_mutex);
				if (m_locked)
				{
					TORRENT_ASSERT(m_alloc.m_locks > 0);
					if (--m_alloc.m_locks == 0)
					{
						TORRENT_ASSERT(m_alloc.m_saved_size != -1);
						if (m_alloc.m_reset_pending)
						{
							m_alloc.m_storage.clear();
							m_alloc.m_reset_pending = false;
						}
						else
						{
							if (reset)
								m_alloc.m_storage.resize(m_alloc.m_saved_size);
						}
						m_alloc.m_saved_size = -1;
						m_alloc.m_consec_locks = 0;
					}
					m_locked = false;
				}
			}

		private:
			// non-copyable
			stack_allocator_lock(stack_allocator_lock const&);
			stack_allocator_lock& operator=(stack_allocator_lock const&);

			stack_allocator& m_alloc;
			bool m_locked = false;
		};

		struct scoped_lock : stack_allocator_lock
		{
			scoped_lock(stack_allocator& alloc, const bool auto_reset = true)
				: stack_allocator_lock(alloc), m_auto_reset(auto_reset) {}
			~scoped_lock() { unlock(m_auto_reset); }

		private:
			// non-copyable
			scoped_lock(scoped_lock const&);
			scoped_lock& operator=(scoped_lock const&);
			bool m_auto_reset;
		};

		stack_allocator() : m_locks(0)
			, m_consec_locks(0)
			, m_saved_size(-1)
			, m_reset_pending(false) {}

		int copy_string(std::string const& str)
		{
			int ret = int(m_storage.size());
			m_storage.resize(ret + str.length() + 1);
			strcpy(&m_storage[ret], str.c_str());
			return ret;
		}

		int copy_string(char const* str)
		{
			int ret = int(m_storage.size());
			int len = strlen(str);
			m_storage.resize(ret + len + 1);
			strcpy(&m_storage[ret], str);
			return ret;
		}

		int copy_buffer(char const* buf, int size)
		{
			int ret = int(m_storage.size());
			if (size < 1) return -1;
			m_storage.resize(ret + size);
			memcpy(&m_storage[ret], buf, size);
			return ret;
		}

		int allocate(int bytes)
		{
			if (bytes < 1) return -1;
			int ret = int(m_storage.size());
			m_storage.resize(ret + bytes);
			return ret;
		}

		char* ptr(int idx)
		{
			if(idx < 0) return NULL;
			TORRENT_ASSERT(idx < int(m_storage.size()));
			return &m_storage[idx];
		}

		char const* ptr(int idx) const
		{
			if(idx < 0) return NULL;
			TORRENT_ASSERT(idx < int(m_storage.size()));
			return &m_storage[idx];
		}

		void swap(stack_allocator& rhs)
		{
			mutex::scoped_lock lock(m_mutex);
			m_storage.swap(rhs.m_storage);
		}

		void reset()
		{
			mutex::scoped_lock lock(m_mutex);
			if (m_locks > 0)
			{
				m_reset_pending = true;
			}
			else
			{
				m_storage.clear();
				m_reset_pending = false;
			}
		}

	private:
		// non-copyable
		stack_allocator(stack_allocator const&);
		stack_allocator& operator=(stack_allocator const&);

		int m_locks;
		int m_consec_locks;
		int m_saved_size;
		bool m_reset_pending;
		mutable mutex m_mutex;
		buffer m_storage;
	};

} }

#endif

