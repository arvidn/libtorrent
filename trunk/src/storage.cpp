/*

Copyright (c) 2003, Arvid Norberg
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

#include <ctime>
#include <iostream>
#include <ios>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <algorithm>
#include <set>

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/bind.hpp>
#include <boost/ref.hpp>

#include "libtorrent/storage.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/session.hpp"

#if defined(_MSC_VER)
#define for if (false) {} else for
#endif

using namespace libtorrent;

//#define SUPER_VERBOSE_LOGGING

namespace {

	struct defered_action_base {};

	template<class T, class U>
	struct defered_value_impl : defered_action_base
	{
		defered_value_impl(T ref_, U enter, U exit_)
			: exit(exit_)
			, ref(ref_)
		{
			ref = enter;
		}

		~defered_value_impl()
		{
			ref = exit;
		}

		mutable U exit;
		T ref;
	};

	template<class T, class U>
	defered_value_impl<T&, U> defered_value(T& x, const U& enter, const U& exit)
	{
		return defered_value_impl<T&, U>(x, enter, exit);
	}

	template<class Fn>
	struct defered_action_impl : defered_action_base
	{
		defered_action_impl(const Fn& fn_)
			: fn(fn_)
		{}

		~defered_action_impl()
		{
			fn();
		}

		Fn fn;
	};

	template<class Fn>
	defered_action_impl<Fn> defered_action(const Fn& fn)
	{
		return defered_action_impl<Fn>(fn);
	}

	typedef const defered_action_base& defered;

	struct defered_bitvector_value : defered_action_base
	{
		defered_bitvector_value(std::vector<bool>& v, std::size_t n_, bool entry_, bool exit_)
			: vec(v)
			, n(n_)
			, onexit(exit_)
		{
			vec[n] = entry_;
		}

		~defered_bitvector_value()
		{
			vec[n] = onexit;
		}

		std::vector<bool>& vec;
		std::size_t n;
		bool onexit;
	};

} // namespace unnamed


void libtorrent::piece_file::reopen()
{
	m_position = m_storage->piece_storage(m_piece_index);

	entry::integer_type global_pos = m_position;

	assert(global_pos >= 0 
			&& global_pos < m_storage->m_torrent_file->total_size());

	for (m_file_iter = m_storage->m_torrent_file->begin_files();
			global_pos > m_file_iter->size; global_pos -= m_file_iter->size);

	m_file_offset = global_pos + m_piece_offset;

	namespace fs = boost::filesystem;

	fs::path path = m_storage->m_save_path / 
		m_file_iter->path / m_file_iter->filename;

	m_file.close();
	m_file.clear();

	m_file.open(path.native_file_string().c_str(), m_file_mode);

	if (m_mode == in) m_file.seekg(m_file_offset, std::ios_base::beg);
	else m_file.seekp(m_file_offset, std::ios_base::beg);
}

void libtorrent::piece_file::lock(bool lock_)
{
	boost::mutex::scoped_lock lock(m_storage->m_locked_pieces_monitor);

	if (lock_)
	{
		while (m_storage->m_locked_pieces[m_piece_index])
			m_storage->m_unlocked_pieces.wait(lock);
		m_storage->m_locked_pieces[m_piece_index] = true;
	}
	else
	{
		m_storage->m_locked_pieces[m_piece_index] = false;
		m_storage->m_unlocked_pieces.notify_all();
	}
}

void libtorrent::piece_file::open(storage* s, int index, open_mode o, int seek_offset, bool lock_)
{
	// do this here so that blocks can be allocated before we lock the piece
	m_position = s->piece_storage(index);

	// synchronization ------------------------------------------------------

	m_storage = s;
	m_piece_index = index;

	lock();
	defered unlock = defered_action(
							boost::bind(
								&piece_file::lock
							  , this
							  , false));

/*	std::vector<bool>& locked_pieces = s->m_locked_pieces;
	boost::mutex::scoped_lock piece_lock(s->m_locked_pieces_monitor);

	while (locked_pieces[index])
		s->m_unlocked_pieces.wait(piece_lock);

	defered notifier = defered_action(
							boost::bind(
								&boost::condition::notify_all
							  , boost::ref(s->m_unlocked_pieces)
							));

	defered piece_locker = defered_bitvector_value(
		locked_pieces, index, true, false);*/
	// ----------------------------------------------------------------------

	assert(index >= 0 && index < s->m_torrent_file->num_pieces() && "internal error");
	
	m_mode = o;
	m_piece_size = m_storage->m_torrent_file->piece_size(m_piece_index);
	m_position = s->piece_storage(index);

	m_piece_offset = seek_offset;

	entry::integer_type global_pos = m_position;

	assert(global_pos >= 0 && global_pos < s->m_torrent_file->total_size());

	for (m_file_iter = m_storage->m_torrent_file->begin_files();
			global_pos > m_file_iter->size; global_pos -= m_file_iter->size);

	m_file_offset = global_pos + m_piece_offset;

	std::ios_base::openmode m;
	if (m_mode == out) m = std::ios_base::out | std::ios_base::in | std::ios_base::binary;
	else m = std::ios_base::in | std::ios_base::binary;

	namespace fs = boost::filesystem;

	fs::path path = m_storage->m_save_path / 
		m_file_iter->path / m_file_iter->filename;

	m_file.close();
	m_file.clear();

	m_file_mode = m;
	m_file.open(path.native_file_string().c_str(), m_file_mode);

	if (m_mode == in) m_file.seekg(m_file_offset, std::ios_base::beg);
	else m_file.seekp(m_file_offset, std::ios_base::beg);

#if 0 // old implementation

	open_mode old_mode = m_mode;
	storage* old_storage = m_storage;
	std::vector<file>::const_iterator old_file_iter = m_file_iter;
	const file* old_file_info = &(*m_file_iter);

	m_mode = o;
	m_piece_index = index;
	m_storage = s;
	m_piece_size = m_storage->m_torrent_file->piece_size(m_piece_index);

	assert(index < m_storage->m_torrent_file->num_pieces() && "internal error");

	m_piece_offset = seek_offset;
	int piece_byte_offset = index * m_storage->m_torrent_file->piece_length()
		+ m_piece_offset;

	entry::integer_type file_byte_offset = 0;
	for (m_file_iter = m_storage->m_torrent_file->begin_files();
		m_file_iter != m_storage->m_torrent_file->end_files();
		++m_file_iter)
	{
		if (file_byte_offset + m_file_iter->size > piece_byte_offset) break;
		file_byte_offset += m_file_iter->size;
	}
	// m_file_iter now refers to the first file this piece is stored in

	// if we're still in the same file, don't reopen it
	if ((m_mode == out && !(m_file_mode & std::ios_base::out))
		|| old_file_iter != m_file_iter
		|| !m_file.is_open()
		|| m_file.fail()
		|| old_storage != m_storage)
	{
		std::ios_base::openmode m;
		if (m_mode == out) m = std::ios_base::out | std::ios_base::in | std::ios_base::binary;
		else m = std::ios_base::in | std::ios_base::binary;

		const file& f = *m_file_iter;
		boost::filesystem::path p = m_storage->m_save_path;
		p /= f.path;
		p /= f.filename;
		m_file.close();
		m_file.clear();

		m_file_mode = m;
		m_file.open(p.native_file_string().c_str(), m_file_mode);
//		std::cout << "opening file: '" << p.native_file_string() << "'\n";
		if (m_file.fail())
		{
			// TODO: try to recover! create a new file?
			assert(!m_file.fail());
		}
	}
	assert(!m_file.fail());

	m_file_offset = piece_byte_offset - file_byte_offset;
	if (m_mode == in) m_file.seekg(m_file_offset, std::ios_base::beg);
	else m_file.seekp(m_file_offset, std::ios_base::beg);

#ifndef NDEBUG
	int gpos = m_file.tellg();
	int ppos = m_file.tellp();
	assert(m_mode == out || m_file_offset == gpos && "internal error");
	assert(m_mode == in || m_file_offset == ppos && "internal error");
#endif

#endif
}

int libtorrent::piece_file::read(char* buf, int size, bool lock_)
{
	assert(m_mode == in);
	assert(!m_file.fail());
	assert(m_file.is_open());

	// synchronization ------------------------------------------------------
/*	std::vector<bool>& locked_pieces = m_storage->m_locked_pieces;
	boost::mutex::scoped_lock piece_lock(m_storage->m_locked_pieces_monitor);

	while (locked_pieces[m_piece_index])
		m_storage->m_unlocked_pieces.wait(piece_lock);

	defered notifier = defered_action(
							boost::bind(
								&boost::condition::notify_all
							  , boost::ref(m_storage->m_unlocked_pieces)
							));
	
	defered piece_locker = defered_bitvector_value(
		locked_pieces, m_piece_index, true, false);*/

	lock();
	defered unlock = defered_action(
							boost::bind(
								&piece_file::lock
							  , this
							  , false));
	// ----------------------------------------------------------------------

	if (m_storage->piece_storage(m_piece_index) != m_position)
	{
		reopen();
	}

	int left_to_read = size;

	if (m_piece_offset + left_to_read > m_piece_size)
		left_to_read = m_piece_size - m_piece_offset;

	int result = left_to_read;
	int buf_pos = 0;

	while (left_to_read > 0)
	{
		int read_bytes = left_to_read;
		if (m_file_offset + read_bytes > m_file_iter->size)
			read_bytes = m_file_iter->size - m_file_offset;

		m_file.read(buf + buf_pos, read_bytes);

		assert(read_bytes == m_file.gcount());

		left_to_read -= read_bytes;
		buf_pos += read_bytes;
		m_file_offset += read_bytes;
		m_piece_offset += read_bytes;

		if (left_to_read > 0)
		{
			++m_file_iter;
			boost::filesystem::path path = m_storage->m_save_path /
				m_file_iter->path / m_file_iter->filename;

			m_file_offset = 0;
			m_file.close();
			m_file.clear();
			m_file.open(path.native_file_string().c_str(), m_file_mode);
		}
	}

	return result;

#if 0 // old implementation
	
	assert(m_file_offset == (long)m_file.tellg() && "internal error");
//	std::cout << std::clock() << "read " << m_piece_index << "\n";

	assert(m_mode == in);
	assert(!m_file.fail());
	assert(m_file.is_open());
	int left_to_read = size;

	// make sure we don't read more than what belongs to this piece
	if (m_piece_offset + left_to_read > m_piece_size)
		left_to_read = m_piece_size - m_piece_offset;

	int buf_pos = 0;
	int read_total = 0;
	do
	{
		assert(m_file_iter != m_storage->m_torrent_file->end_files() && "internal error, please report!");
		int available = std::min(static_cast<entry::integer_type>(m_file_iter->size - m_file_offset),
			static_cast<entry::integer_type>(left_to_read));

		m_file.read(buf + buf_pos, available);
		int read = m_file.gcount();
		left_to_read -= read;
		read_total += read;
		buf_pos += read;
		m_file_offset += read;
		m_piece_offset += read;

		if (m_file_offset == m_file_iter->size && m_piece_offset < m_piece_size)
		{
			++m_file_iter;
			assert(m_file_iter != m_storage->m_torrent_file->end_files() && "internal error, please report!");
			boost::filesystem::path path = m_storage->m_save_path;
			path /= m_file_iter->path;
			path /= m_file_iter->filename;

			m_file_offset = 0;
			m_file.close();
			m_file.clear();
			m_file.open(path.native_file_string().c_str(), m_file_mode);
//			std::cout << "opening file: '" << path.native_file_string() << "'\n";
			if (m_file.fail())
			{
				// TODO: try to recover! create a new file?
				assert(!m_file.fail());
			}
		}
		else
		{
			assert(read != 0 && "internal error");
		}
	} while (left_to_read > 0);

#ifndef NDEBUG
	int gpos = m_file.tellg();
	assert(m_file_offset == gpos && "internal error");
#endif
	return read_total;

#endif
}

void libtorrent::piece_file::write(const char* buf, int size, bool lock_)
{
	assert(m_mode == out);
	assert(!m_file.fail());
	assert(m_file.is_open());

	// synchronization ------------------------------------------------------
/*	std::vector<bool>& locked_pieces = m_storage->m_locked_pieces;
	boost::mutex::scoped_lock piece_lock(m_storage->m_locked_pieces_monitor);

	while (locked_pieces[m_piece_index])
		m_storage->m_unlocked_pieces.wait(piece_lock);

	defered notifier = defered_action(
							boost::bind(
								&boost::condition::notify_all
							  , boost::ref(m_storage->m_unlocked_pieces)
							));

	defered piece_locker = defered_bitvector_value(
		locked_pieces, m_piece_index, true, false);*/
	lock();
	defered unlock = defered_action(
							boost::bind(
								&piece_file::lock
							  , this
							  , false));

	// ----------------------------------------------------------------------

	if (m_storage->piece_storage(m_piece_index) != m_position)
	{
		reopen();
	}

	int left_to_write = size;

	if (m_piece_offset + left_to_write > m_piece_size)
		left_to_write = m_piece_size - m_piece_offset;

	int buf_pos = 0;

	while (left_to_write > 0)
	{
		int write_bytes = left_to_write;
		if (m_file_offset + write_bytes > m_file_iter->size)
			write_bytes = m_file_iter->size - m_file_offset;

		m_file.write(buf + buf_pos, write_bytes);

		left_to_write -= write_bytes;
		buf_pos += write_bytes;
		m_file_offset += write_bytes;
		m_piece_offset += write_bytes;

		if (left_to_write > 0)
		{
			++m_file_iter;

			assert(m_file_iter != m_storage->m_torrent_file->end_files());

			boost::filesystem::path path = m_storage->m_save_path /
				m_file_iter->path / m_file_iter->filename;

			m_file_offset = 0;
			m_file.close();
			m_file.clear();
			m_file.open(path.native_file_string().c_str(), m_file_mode);
		}
	}

#if 0 // old implementation
	
	assert(m_mode == out);
	int left_to_write = size;

	// make sure we don't write more than what belongs to this piece
	if (m_piece_offset + left_to_write > m_piece_size)
		left_to_write = m_piece_size - m_piece_offset;

	int buf_pos = 0;
	do
	{
		assert(m_file_iter != m_storage->m_torrent_file->end_files() && "internal error, please report!");
		int this_write = std::min(static_cast<entry::integer_type>(m_file_iter->size - m_file_offset),
			static_cast<entry::integer_type>(left_to_write));

		m_file.write(buf + buf_pos, this_write);
		left_to_write -= this_write;
		buf_pos += this_write;
		m_file_offset += this_write;
		m_piece_offset += this_write;

		if (m_file_offset == m_file_iter->size && m_piece_offset < m_piece_size)
		{
			++m_file_iter;
			assert(m_file_iter != m_storage->m_torrent_file->end_files() && "internal error, please report!");
			boost::filesystem::path path = m_storage->m_save_path;
			path /= m_file_iter->path;
			path /= m_file_iter->filename;

			m_file_offset = 0;
			m_file.close();
			m_file.open(path.native_file_string().c_str(), m_file_mode);
//			std::cout << "opening file: '" << path.native_file_string() << "'\n";
			if (m_file.fail())
			{
				// TODO: try to recover! create a new file?
				assert(!m_file.fail());
			}
		}
	} while (left_to_write > 0);

#endif
}

void libtorrent::piece_file::seek_forward(int step, bool lock_)
{
	lock();
	defered unlock = defered_action(
							boost::bind(
								&piece_file::lock
							  , this
							  , false));

	assert(step >= 0 && "you cannot seek backwards in piece files");
	assert(m_file_offset == (long)m_file.tellg() && "internal error");
	if (step == 0) return;

	int left_to_seek = step;

	// make sure we don't read more than what belongs to this piece
	assert(m_piece_offset + step <= m_piece_size);

	assert(m_file_iter != m_storage->m_torrent_file->end_files() && "internal error, please report!");
	bool reopen = false;
	while (m_file_iter->size - m_file_offset < left_to_seek)
	{
		left_to_seek -= m_file_iter->size - m_file_offset;
		m_piece_offset += m_file_iter->size - m_file_offset;
		m_file_offset = 0;
		++m_file_iter;
		reopen = true;
		assert(m_file_iter != m_storage->m_torrent_file->end_files() && "internal error, please report!");
	}

	if (reopen)
	{
		boost::filesystem::path path = m_storage->m_save_path;
		path /= m_file_iter->path;
		path /= m_file_iter->filename;

		m_file.close();
		m_file.open(path.native_file_string().c_str(), std::ios_base::in | std::ios_base::binary);
	}

	m_file_offset += left_to_seek;
	m_piece_offset +=left_to_seek;

	if (m_mode == in)
	{
		m_file.seekg(left_to_seek, std::ios_base::cur);
		assert(m_file_offset == (long)m_file.tellg() && "internal error");
	}
	else
	{
		m_file.seekp(left_to_seek, std::ios_base::cur);
		assert(m_file_offset == (long)m_file.tellp() && "internal error");
	}

}


bool libtorrent::storage::verify_piece(piece_file& file)
{
	int index = file.index();
	assert(index >= 0 && index < m_have_piece.size());
	if (m_have_piece[index]) return true;

	std::vector<char> buffer(m_torrent_file->piece_size(index));

	file.open(this, index, piece_file::in);
	int read = file.read(&buffer[0], buffer.size());

	assert(read == m_torrent_file->piece_size(index));

	// calculate hash for piece
	hasher h;
	h.update(&buffer[0], read);
	sha1_hash digest = h.final();

	if (std::equal(digest.begin(), digest.end(), m_torrent_file->hash_for_piece(index).begin()))
	{
		// tell all peer_connections to announce that
		// this piece is available
		m_bytes_left -= read;

		// mark it as available
		m_have_piece[index] = true;

#ifndef NDEBUG
		int real_bytes_left = 0;
		for (int i = 0; i < m_torrent_file->num_pieces(); ++i)
		{
			if (!m_have_piece[i]) real_bytes_left += m_torrent_file->piece_size(i);
		}
		assert(real_bytes_left == m_bytes_left);
#endif

		return true;
	}
	return false;
}

namespace {

	struct equal_hash
	{
		bool operator()(const sha1_hash& lhs, const sha1_hash& rhs) const
		{
			return std::equal(lhs.begin(), lhs.end(), rhs.begin());
		}
	};

	struct lazy_hash
	{
		mutable sha1_hash digest;
		mutable hasher h;
		mutable const char* data;
		std::size_t size;

		lazy_hash(const char* data_, std::size_t size_)
			: data(data_)
			, size(size_)
		{}

		const sha1_hash& get() const
		{
			if (data)
			{
				h.update(data, size);
				digest = h.final();
				data = 0;
			}
			return digest;
		}
	};

	void print_bitmask(const std::vector<bool>& x)
	{
		for (std::size_t i = 0; i < x.size(); ++i)
		{
			std::cout << x[i];
		}
	}

} // namespace unnamed

void libtorrent::storage::allocate_pieces(int num)
{
	// synchronization ------------------------------------------------------
	boost::recursive_mutex::scoped_lock lock(m_mutex);
	// ----------------------------------------------------------------------

	namespace fs = boost::filesystem;
	
	std::cout << "allocating pieces...\n";

	std::vector<entry::integer_type>::iterator iter
		= m_free_blocks.begin();
	std::vector<entry::integer_type>::iterator end_iter 
		= m_free_blocks.end();

	const entry::integer_type piece_size = m_torrent_file->piece_length();

	std::vector<char> zeros(piece_size, 0);
	
	int last_piece_index = -1;

	for (int i = 0; i < num; ++i, ++iter)
	{
		if (iter == end_iter)
			break;

		entry::integer_type pos = *iter;
		entry::integer_type piece_pos = pos;

		const bool last_piece = 
			pos == m_torrent_file->total_size() 
					- m_torrent_file->piece_size(
							m_torrent_file->num_pieces() - 1);

		if (last_piece)
			last_piece_index = i;

		torrent_info::file_iterator file_iter;

		for (file_iter = m_torrent_file->begin_files();
				pos > file_iter->size; ++file_iter)
		{
			pos -= file_iter->size;
		}

		entry::integer_type bytes_left = last_piece 
			? m_torrent_file->piece_size(m_torrent_file->num_pieces() - 1)
			: piece_size;

		while (bytes_left > 0)
		{
//			fs::ofstream out(fs::path("foo.bar"), std::ios_base::binary | std::ios_base::in);

			fs::path path(m_save_path / file_iter->path / file_iter->filename);

			fs::ofstream out;

			if (fs::exists(path))
				out.open(path, std::ios_base::binary | std::ios_base::in);
			else
				out.open(path, std::ios_base::binary);

//			std::ofstream out((m_save_path / file_iter->path / file_iter->filename).native_file_string().c_str()
//				, std::ios_base::binary | std::ios_base::in);

			out.seekp(pos, std::ios_base::beg);

			assert((entry::integer_type)out.tellp() == pos);

			entry::integer_type bytes_to_write = bytes_left;

			if (pos + bytes_to_write >= file_iter->size)
			{
				bytes_to_write = file_iter->size - pos;
			}

			out.write(&zeros[0], bytes_to_write);

			bytes_left -= bytes_to_write;
			++file_iter;
			pos = 0;
		}

		m_free_pieces.push_back(piece_pos);
		m_slot_to_piece[piece_pos / piece_size] = -2;
	}

	m_free_blocks.erase(m_free_blocks.begin(), iter);

	// move last slot to the end
	if (last_piece_index != -1)
		std::swap(m_free_pieces[last_piece_index], m_free_pieces.front());

	std::cout << "\tfree pieces: " << m_free_pieces.size() << "\n";
	std::cout << "\tfree blocks: " << m_free_blocks.size() << "\n";
}

#include <sstream>

entry::integer_type libtorrent::storage::piece_storage(int piece)
{
	// synchronization ------------------------------------------------------
	boost::recursive_mutex::scoped_lock lock(m_mutex);
	// ----------------------------------------------------------------------

	assert(piece >= 0 && piece < m_allocated_pieces.size());

	entry::integer_type result;

	result = m_allocated_pieces[piece];

	if (result != -1)
	{
			return result;
	}

	if (m_free_pieces.empty())
	{
		allocate_pieces(5);
		assert(!m_free_pieces.empty());
	}

	entry::integer_type wanted_pos = piece * m_torrent_file->piece_length();

	std::vector<entry::integer_type>::iterator iter(
		std::find(
			m_free_pieces.begin()
		  , m_free_pieces.end()
		  , wanted_pos));

	if (iter == m_free_pieces.end())
		iter = m_free_pieces.end() - 1;

	result = *iter;
	m_free_pieces.erase(iter);

//	assert(m_slot_to_piece[result / m_torrent_file->piece_length()] < 0);

	m_slot_to_piece[result / m_torrent_file->piece_length()] = piece;
	m_allocated_pieces[piece] = result;

	// the last slot can only be given to the last piece!
	// if this is the last slot, swap position with the last piece
	const int last_piece = m_torrent_file->num_pieces() - 1;
	const bool last_slot = result == 
		m_torrent_file->total_size() - m_torrent_file->piece_size(last_piece);

	if (last_slot && piece != last_piece)
	{
		assert(m_allocated_pieces[last_piece] != -1);

		piece_file f;

		f.open(this, last_piece, piece_file::in);
		std::vector<char> buf(m_torrent_file->piece_size(last_piece));
		f.read(&buf[0], m_torrent_file->piece_size(last_piece));
		f.close();

		f.open(this, piece, piece_file::out);
		f.write(&buf[0], m_torrent_file->piece_size(last_piece));
		f.close();

		std::swap(
			m_slot_to_piece[m_allocated_pieces[piece] / m_torrent_file->piece_length()]
		  , m_slot_to_piece[m_allocated_pieces[last_piece] / m_torrent_file->piece_length()]
		);

		std::swap(m_allocated_pieces[piece], m_allocated_pieces[last_piece]);
		result = m_allocated_pieces[piece];
	}
	else
	{
		int my_slot = result / m_torrent_file->piece_length();
		int other_piece = m_slot_to_piece[piece];

		if (piece == my_slot)
			return result;

		// the slot that we want is available and
		// some other piece want slot
		if (m_allocated_pieces[my_slot] >= 0 // another piece wants this position
		 && other_piece >= 0
		 && m_slot_to_piece[piece] != my_slot) // .. and we want another piece position)
		{	
			piece_file f;

			std::vector<char> buf1(m_torrent_file->piece_length());
			std::vector<char> buf2(m_torrent_file->piece_length());

#ifdef SUPER_VERBOSE_LOGGING
			std::stringstream s;

			s << "double move!\n";

			s << "allocating for piece #" << piece << " at slot " << my_slot << "\n";
			s << "  piece at my wanted storage: " << other_piece << "\n";
			s << "  slot that wants my storage: " << 
				m_allocated_pieces[my_slot] / m_torrent_file->piece_length() << "\n";

			for (int i = 0; i < m_torrent_file->num_pieces(); ++i)
			{
				if (m_slot_to_piece[i] == i)
					s << i << " is in correct position\n";
				else if (m_slot_to_piece[i] < 0)
					s << i << " is not used\n";
				else
					s << i << " is in wrong position (" << m_slot_to_piece[i] << ")\n";
			}
#endif

			// m_allocated_pieces[my_slot] -> piece
			// other_piece -> m_allocated_pieces[my_slot]
			// piece -> other_piece

			// read piece that wants my storage
			f.open(this, my_slot, piece_file::in);
			f.read(&buf1[0], m_torrent_file->piece_length());
			f.close();

			// read piece that should be moved away
			f.open(this, other_piece, piece_file::in);
			f.read(&buf2[0], m_torrent_file->piece_length());
			f.close();

			// write piece that wants my storage
			f.open(this, piece, piece_file::out);
			f.write(&buf1[0], m_torrent_file->piece_length());
			f.close();

			// write piece that should be moved away
			f.open(this, my_slot, piece_file::out);
			f.write(&buf2[0], m_torrent_file->piece_length());
			f.close();

			entry::integer_type pos[3] = {
				m_allocated_pieces[piece]						// me
			  , m_allocated_pieces[my_slot]					// piece that wants my storage
			  , m_allocated_pieces[other_piece]			// piece that is moved away
			};

			int slots[3] = {
				my_slot				//me 
			  , m_allocated_pieces[my_slot] / m_torrent_file->piece_length()
												// piece that wants my storage
			  , piece					// piece that is moved away
			};

			m_slot_to_piece[my_slot] = my_slot;
			m_slot_to_piece[piece] = piece;
			m_slot_to_piece[
					m_allocated_pieces[my_slot] / m_torrent_file->piece_length()
				] = other_piece;

			m_allocated_pieces[piece] = pos[2];
			m_allocated_pieces[my_slot] = pos[0];
			m_allocated_pieces[other_piece] = pos[1];

#ifdef SUPER_VERBOSE_LOGGING
			s << "i want slot       : #" << piece << "\n";
			s << "occupied by piece : #" << other_piece << "\n";
			s << "wants my slot     : #" << my_slot << "\n";
			
			for (int i = 0; i < m_torrent_file->num_pieces(); ++i)
			{
				if (m_slot_to_piece[i] == i)
					s << i << " is in correct position\n";
				else if (m_slot_to_piece[i] < 0)
					s << i << " is not used\n";
				else
					s << i << " is in wrong position (" << m_slot_to_piece[i] << ")\n";
			}
			
			m_torrent->debug_log(s.str());
#endif			

			return m_allocated_pieces[piece];
		}

		//  there's another piece that wans my slot, swap positions
		if (m_allocated_pieces[my_slot] > 0 && m_slot_to_piece[piece] != my_slot)
		{
			piece_file f;
#ifdef SUPER_VERBOSE_LOGGING
			std::stringstream s;

			s << "single move!\n";

			s << "allocating for piece #" << piece << " at slot " << my_slot << "\n";
			s << "  slot that wants my storage: " << 
				m_allocated_pieces[my_slot] / m_torrent_file->piece_length() << "\n";

			for (int i = 0; i < m_torrent_file->num_pieces(); ++i)
			{
				if (m_slot_to_piece[i] == i)
					s << i << " is in correct position\n";
				else if (m_slot_to_piece[i] < 0)
					s << i << " is not used\n";
				else
					s << i << " is in wrong position (" << m_slot_to_piece[i] << ")\n";
			}
#endif
			f.open(this, my_slot, piece_file::in);
			std::vector<char> buf(m_torrent_file->piece_length());
			f.read(&buf[0], m_torrent_file->piece_length());
			f.close();

			f.open(this, piece, piece_file::out);
			f.write(&buf[0], m_torrent_file->piece_length());
			f.close();

			std::swap(
				m_slot_to_piece[my_slot]
				, m_slot_to_piece[m_allocated_pieces[my_slot] / m_torrent_file->piece_length()]
			);

			std::swap(m_allocated_pieces[piece], m_allocated_pieces[my_slot]);
#ifdef SUPER_VERBOSE_LOGGING
			s << "-------------\n";

			for (int i = 0; i < m_torrent_file->num_pieces(); ++i)
			{
				if (m_slot_to_piece[i] == i)
					s << i << " is in correct position\n";
				else if (m_slot_to_piece[i] < 0)
					s << i << " is not used\n";
				else
					s << i << " is in wrong position (" << m_slot_to_piece[i] << ")\n";
			}

			m_torrent->debug_log(s.str());
#endif
			return m_allocated_pieces[piece];
		}

		if (other_piece >= 0 && other_piece != piece)
		{
			piece_file f;

			f.open(this, other_piece, piece_file::in);
			std::vector<char> buf(m_torrent_file->piece_length());
			f.read(&buf[0], m_torrent_file->piece_length());
			f.close();

			f.open(this, piece, piece_file::out);
			f.write(&buf[0], m_torrent_file->piece_length());
			f.close();

			std::swap(
				m_slot_to_piece[piece]
			  , m_slot_to_piece[result / m_torrent_file->piece_length()]
			);

			std::swap(m_allocated_pieces[piece], m_allocated_pieces[other_piece]);

#ifdef SUPER_VERBOSE_LOGGING
			std::stringstream s;

			s << "\nswapping #" << piece << " into place\n";
			s << "moved #" << other_piece << " doing it..\n";

			for (int i = 0; i < m_torrent_file->num_pieces(); ++i)
			{
				if (m_slot_to_piece[i] == i)
					s << i << " is in correct position\n";
				else if (m_slot_to_piece[i] < 0)
					s << i << " is not used\n";
				else
					s << i << " is in wrong position (" << m_slot_to_piece[i] << ")\n";
			}
			
			m_torrent->debug_log(s.str());
#endif
			return m_allocated_pieces[piece];
		}

		assert(false);
	}

	return result;
}

void libtorrent::storage::initialize_pieces(torrent* t,
	const boost::filesystem::path& path,
	detail::piece_checker_data* data,
	boost::mutex& mutex)
{
	// synchronization ------------------------------------------------------
	boost::recursive_mutex::scoped_lock lock(m_mutex);
	// ----------------------------------------------------------------------

	namespace fs = boost::filesystem;

	m_torrent = t;
	
	m_save_path = path;
	m_torrent_file = &t->torrent_file();

	// free up some memory
	std::vector<bool>(
		m_torrent_file->num_pieces(), false
	).swap(m_have_piece);

	std::vector<entry::integer_type>(
		m_torrent_file->num_pieces(), -1
	).swap(m_allocated_pieces);

	std::vector<bool>(
		m_torrent_file->num_pieces(), false
	).swap(m_locked_pieces);

	std::vector<int>(
		m_torrent_file->num_pieces(), -1
	).swap(m_slot_to_piece);

	std::vector<entry::integer_type>().swap(m_free_blocks);
	std::vector<entry::integer_type>().swap(m_free_pieces);

	m_bytes_left = m_torrent_file->total_size();

	const std::size_t piece_size = m_torrent_file->piece_length();
	const std::size_t last_piece_size = m_torrent_file->piece_size(
			m_torrent_file->num_pieces() - 1);

	bool changed_file = true;
	fs::ifstream in;

	std::vector<char> piece_data(m_torrent_file->piece_length());
	std::size_t piece_offset = 0;

	std::size_t current_piece = 0;
	std::size_t bytes_to_read = piece_size;
	std::size_t bytes_current_read = 0;
	std::size_t seek_into_next = 0;
	entry::integer_type filesize = 0;
	entry::integer_type start_of_read = 0;
	entry::integer_type start_of_file = 0;

	for (torrent_info::file_iterator file_iter = m_torrent_file->begin_files(),
		  end_iter = m_torrent_file->end_files(); 
		  file_iter != end_iter;)
	{
		{
			boost::mutex::scoped_lock lock(mutex);

			if (data->abort)
				;
		}

		fs::path path(m_save_path / file_iter->path);

		// if the path doesn't exist, create the
		// entire directory tree
		if (!fs::exists(path))
			fs::create_directories(path);

		path /= file_iter->filename;

		if (changed_file)
		{
			in.close();
			in.clear();
			in.open(path, std::ios_base::binary);

			changed_file = false;

			bytes_current_read = seek_into_next;

			if (!in)
			{
				filesize = 0;
			}
			else
			{
				in.seekg(0, std::ios_base::end);
				filesize = in.tellg();
				in.seekg(seek_into_next, std::ios_base::beg);
			}
		}

		// we are at the start of a new piece
		// so we store the start of the piece
		if (bytes_to_read == m_torrent_file->piece_size(current_piece))
			start_of_read = current_piece * piece_size;

		std::size_t bytes_read = 0;

		if (filesize > 0)
		{
			in.read(&piece_data[piece_offset], bytes_to_read);
			bytes_read = in.gcount();
		}

		bytes_current_read += bytes_read;
		bytes_to_read -= bytes_read;

		assert(bytes_to_read >= 0);
		
		// bytes left to read, go on with next file
		if (bytes_to_read > 0)
		{
			if (bytes_current_read != file_iter->size)
			{
				entry::integer_type pos;
				entry::integer_type file_end = start_of_file + file_iter->size;

				for (pos = start_of_read; pos < file_end;
						pos += piece_size)
				{
					m_free_blocks.push_back(pos);
					++current_piece;
				}

				seek_into_next = pos - file_end;
				bytes_to_read = piece_size;
				piece_offset = 0;
			}
			else
			{
				seek_into_next = 0;
				piece_offset += bytes_read;
			}

			changed_file = true;
			start_of_file += file_iter->size;
			++file_iter;
			continue;
		}

		// done with piece, move on to next
		piece_offset = 0;
		++current_piece;
		
		// we need to take special actions if this is 
		// the last piece, since that piece might actually 
		// be smaller than piece_size.

		lazy_hash large_digest(&piece_data[0], piece_size);
		lazy_hash small_digest(&piece_data[0], last_piece_size);
		
		const lazy_hash* digest[2] = {
			&large_digest, &small_digest
		};

		bool found = false;

		for (int i = 0; i < m_torrent_file->num_pieces(); ++i)
		{
			if (m_have_piece[i])
				continue;

			const sha1_hash& hash = digest[
				i == m_torrent_file->num_pieces() - 1]->get();

			if (equal_hash()(hash, m_torrent_file->hash_for_piece(i)))
			{
				m_bytes_left -= m_torrent_file->piece_size(i);

				m_allocated_pieces[i] = start_of_read;
				m_slot_to_piece[start_of_read / piece_size] = i;
				m_have_piece[i] = true;
				found = true;
				break;
			}
		}

		if (!found)
		{
			m_slot_to_piece[start_of_read / piece_size] = -2;

			entry::integer_type last_pos =
				m_torrent_file->total_size() - 
					m_torrent_file->piece_size(
						m_torrent_file->num_pieces() - 1);

			// treat the last slot as unallocated space
			// this means that when we get to the last
			// slot we are either allocating space for
			// the last piece, or the last piece has already
			// been allocated
			if (start_of_read == last_pos)
				m_free_blocks.push_back(start_of_read);
			else
				m_free_pieces.push_back(start_of_read);
		}

		bytes_to_read = m_torrent_file->piece_size(current_piece);
	}

	std::cout << " free pieces: " << m_free_pieces.size() << "\n";
	std::cout << " free blocks: " << m_free_blocks.size() << "\n";
	std::cout << " num pieces: " << m_torrent_file->num_pieces() << "\n";

	std::cout << " have_pieces: ";
	print_bitmask(m_have_piece);
	std::cout << "\n";
	std::cout << std::count(m_have_piece.begin(), m_have_piece.end(), true) << "\n";
}

#if 0 // OLD STORAGE

// allocate files will create all files that are missing
// if there are some files that already exists, it checks
// that they have the correct filesize
// data is the structure that is shared between the
// thread where this function is run in and the
// main thread. It is used to communicate progress
// and abortion information.
void libtorrent::storage::initialize_pieces(torrent* t,
	const boost::filesystem::path& path,
	detail::piece_checker_data* data,
	boost::mutex& mutex)
{
	m_save_path = path;
	m_torrent_file = &t->torrent_file();

	// TEMPORARY!
/*
	m_bytes_left = 0;
	m_have_piece.resize(m_torrent_file->num_pieces());
	std::fill(m_have_piece.begin(), m_have_piece.end(), true);
	return;
*/
/*
	m_bytes_left = m_torrent_file->total_size();
	m_have_piece.resize(m_torrent_file->num_pieces());
	std::fill(m_have_piece.begin(), m_have_piece.end(), false);
	return;
*/


	// we don't know of any piece we have right now. Initialize
	// it to say we don't have anything and fill it in later on.
	m_have_piece.resize(m_torrent_file->num_pieces());
	std::fill(m_have_piece.begin(), m_have_piece.end(), false);

	m_bytes_left = m_torrent_file->total_size();

#ifndef NDEBUG
	std::size_t sum = 0;
		for (int i = 0; i < m_torrent_file->num_pieces(); ++i)
			sum += m_torrent_file->piece_size(i);
		assert(sum == m_bytes_left);
#endif

	// this will be set to true if some file already exists
	// in which case another pass will be made to check
	// the hashes of all pieces to know which pieces we
	// have
	bool resume = false;

	unsigned int total_bytes = m_torrent_file->total_size();
	unsigned int progress = 0;

	// the buffersize of the file writes
	const int chunksize = 8192;
	char zeros[chunksize];
	std::fill(zeros, zeros+chunksize, 0);

	// remember which directories we have created, so
	// we don't have to ask the filesystem all the time
	std::set<std::string> created_directories;
	for (torrent_info::file_iterator i = m_torrent_file->begin_files(); i != m_torrent_file->end_files(); ++i)
	{
		boost::filesystem::path path = m_save_path;
		path /= i->path;
		if (created_directories.find(i->path) == created_directories.end())
		{
			if (boost::filesystem::exists(path))
			{
				if (!boost::filesystem::is_directory(path))
				{
					std::string msg = "Cannot create directory, the file \"";
					msg += path.native_file_string();
					msg += "\" is in the way.";
					throw file_allocation_failed(msg.c_str());
				}
			}
			else
			{
				boost::filesystem::create_directories(path);
			}
			created_directories.insert(i->path);
		}

		// allocate the file.
		// fill it with zeros
		path /= i->filename;
		if (boost::filesystem::exists(path))
		{
			// the file exists, make sure it isn't a directory
			if (boost::filesystem::is_directory(path))
			{
				std::string msg = "Cannot create file, the directory \"";
				msg += path.native_file_string();
				msg += "\" is in the way.";
				throw file_allocation_failed(msg.c_str());
			}
//			std::cout << "creating file: '" << path.native_file_string() << "'\n";
			std::ifstream f(path.native_file_string().c_str(), std::ios_base::binary);
			f.seekg(0, std::ios_base::end);
			int filesize = f.tellg();
			if (filesize != i->size)
			{

				// TODO: recover by padding the file with 0
				std::string msg = "The file \"";
				msg += path.native_file_string();
				msg += "\" has the wrong size.";
				throw file_allocation_failed(msg.c_str());
			}
			resume = true;
		}
		else
		{
			// The file doesn't exist, create it and fill it with zeros
			std::ofstream f(path.native_file_string().c_str(), std::ios_base::binary);
			entry::integer_type left_to_write = i->size;
			while(left_to_write >= chunksize)
			{
				f.write(zeros, chunksize);
				// TODO: Check if disk is full
				left_to_write -= chunksize;
				progress += chunksize;

				boost::mutex::scoped_lock l(mutex);
				data->progress = static_cast<float>(progress) / total_bytes;
				if (data->abort) return;
			}
			// TODO: Check if disk is full
			if (left_to_write > 0) f.write(zeros, left_to_write);
			progress += left_to_write;

			boost::mutex::scoped_lock l(mutex);
			data->progress = static_cast<float>(progress) / total_bytes;
			if (data->abort) return;
		}
	}

	// we have to check which pieces we have and which we don't have
	if (resume)
	{
		int missing = 0;
//		std::cout << "checking existing files\n";

		int num_pieces = m_torrent_file->num_pieces();

		progress = 0;
		piece_file f;
		for (unsigned int i = 0; i < num_pieces; ++i)
		{
			f.open(this, i, piece_file::in);
			if (!verify_piece(f)) missing++;

//			std::cout << i+1 << " / " << m_torrent_file->num_pieces() << " missing: " << missing << "\r";

			progress += m_torrent_file->piece_size(i);
			boost::mutex::scoped_lock l(mutex);
			data->progress = static_cast<float>(progress) / total_bytes;
			if (data->abort) return;
		}
//		std::cout << "\n";
	}

}

#endif

