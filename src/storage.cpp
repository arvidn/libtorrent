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
#include "libtorrent/peer_id.hpp"

#if defined(_MSC_VER)
#define for if (false) {} else for
#endif
/*
using namespace libtorrent;

//#define SUPER_VERBOSE_LOGGING
#define NO_THREAD_SAFE_PIECE_FILE

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

	entry::integer_type global_pos = m_position + m_piece_offset;

	assert(global_pos >= 0 
			&& global_pos < m_storage->m_torrent_file->total_size());

	for (m_file_iter = m_storage->m_torrent_file->begin_files();
			global_pos >= m_file_iter->size; ++m_file_iter)
	{
		global_pos -= m_file_iter->size;
	}

	m_file_offset = global_pos;

	namespace fs = boost::filesystem;

	fs::path path = m_storage->m_save_path / 
		m_file_iter->path / m_file_iter->filename;

	m_file.close();
	m_file.clear();

	m_file.open(path, m_file_mode);

	if (m_mode == in) m_file.seekg(m_file_offset, std::ios_base::beg);
	else m_file.seekp(m_file_offset, std::ios_base::beg);
}

void libtorrent::piece_file::lock(bool lock_)
{
#ifndef NO_THREAD_SAFE_PIECE_FILE
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
#endif
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

	// ----------------------------------------------------------------------

	assert(index >= 0 && index < s->m_torrent_file->num_pieces() && "internal error");
	
	m_mode = o;
	m_piece_size = m_storage->m_torrent_file->piece_size(m_piece_index);
	m_position = s->piece_storage(index);

	m_piece_offset = seek_offset;

	entry::integer_type global_pos = m_position + m_piece_offset;

	assert(global_pos >= 0 && global_pos < s->m_torrent_file->total_size());

	for (m_file_iter = m_storage->m_torrent_file->begin_files();
			global_pos >= m_file_iter->size; ++m_file_iter)
	{
		global_pos -= m_file_iter->size;
	}

	m_file_offset = global_pos;

	std::ios_base::openmode m;
	if (m_mode == out) m = std::ios_base::out | std::ios_base::in | std::ios_base::binary;
	else m = std::ios_base::in | std::ios_base::binary;

	namespace fs = boost::filesystem;

	fs::path path = m_storage->m_save_path / 
		m_file_iter->path / m_file_iter->filename;

	m_file.close();
	m_file.clear();

	m_file_mode = m;
	m_file.open(path, m_file_mode);

	if (m_mode == in) m_file.seekg(m_file_offset, std::ios_base::beg);
	else m_file.seekp(m_file_offset, std::ios_base::beg);
}

int libtorrent::piece_file::read(char* buf, int size, bool lock_)
{
	assert(m_mode == in);
	assert(!m_file.fail());
	assert(m_file.is_open());

	// synchronization ------------------------------------------------------
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
		assert(read_bytes > 0);

		m_file.read(buf + buf_pos, read_bytes);

		assert(read_bytes == m_file.gcount());

		left_to_read -= read_bytes;
		buf_pos += read_bytes;
		assert(buf_pos >= 0);
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
			m_file.open(path, m_file_mode);
		}
	}

	return result;
}

void libtorrent::piece_file::write(const char* buf, int size, bool lock_)
{
	assert(m_mode == out);
	assert(!m_file.fail());
	assert(m_file.is_open());

	// synchronization ------------------------------------------------------
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
		{
			assert(m_file_iter->size > m_file_offset);
			write_bytes = m_file_iter->size - m_file_offset;
		}

		assert(buf_pos >= 0);
		assert(write_bytes > 0);
		m_file.write(buf + buf_pos, write_bytes);

		left_to_write -= write_bytes;
		buf_pos += write_bytes;
		assert(buf_pos >= 0);
		m_file_offset += write_bytes;
		assert(m_file_offset <= m_file_iter->size);
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
			m_file.open(path, m_file_mode);
		}
	}
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
		m_file.open(path, std::ios_base::in | std::ios_base::binary);
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
*/
namespace {
/*
	struct equal_hash
	{
		bool operator()(const sha1_hash& lhs, const sha1_hash& rhs) const
		{
			return std::equal(lhs.begin(), lhs.end(), rhs.begin());
		}
	};
*/
	struct lazy_hash
	{
		mutable libtorrent::sha1_hash digest;
		mutable libtorrent::hasher h;
		mutable const char* data;
		std::size_t size;

		lazy_hash(const char* data_, std::size_t size_)
			: data(data_)
			, size(size_)
		{}

		const libtorrent::sha1_hash& get() const
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
/*
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
			fs::path path(m_save_path / file_iter->path / file_iter->filename);

			fs::ofstream out;

			if (fs::exists(path))
				out.open(path, std::ios_base::binary | std::ios_base::in);
			else
				out.open(path, std::ios_base::binary);

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

	int n = m_free_pieces.size();
	int m = m_free_blocks.size();

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

	{
		boost::mutex::scoped_lock lock(mutex);
		data->progress = 0.f;
	}

	for (torrent_info::file_iterator file_iter = m_torrent_file->begin_files(),
		  end_iter = m_torrent_file->end_files(); 
		  file_iter != end_iter;)
	{
		{
			boost::mutex::scoped_lock lock(mutex);

			data->progress = (float)current_piece / m_torrent_file->num_pieces();
			if (data->abort)
				return;
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

	invariant();
}

void libtorrent::storage::invariant() const
{
	// synchronization ------------------------------------------------------
	boost::recursive_mutex::scoped_lock lock(m_mutex);
	// ----------------------------------------------------------------------

	for (int i = 0; i < m_torrent_file->num_pieces(); ++i)
	{
		if (m_allocated_pieces[i] != m_torrent_file->piece_length() * i)
			assert(m_slot_to_piece[i] < 0);
	}
}

libtorrent::storage::size_type libtorrent::storage::read(
	char* buf
  , int slot
  , size_type offset
  , size_type size)
{
	namespace fs = boost::filesystem;

	size_type start = slot * m_torrent_file->piece_length() + offset;

	// find the file iterator and file offset
	size_type file_offset = start;
	std::vector<file>::const_iterator file_iter;

	for (file_iter = m_torrent_file->begin_files();;)
	{
		if (file_offset < file_iter->size)
			break;

		file_offset -= file_iter->size;
		++file_iter;
	}

	fs::ifstream in(
		m_save_path / file_iter->path / file_iter->filename
	  , std::ios_base::binary
	);

	assert(file_offset < file_iter->size);

	in.seekg(std::ios_base::beg, file_offset);

	size_type left_to_read = size;
	size_type slot_size = m_torrent_file->piece_size(slot);

	if (offset + left_to_read > slot_size)
		left_to_read = slot_size - offset;

	assert(left_to_read >= 0);

	int result = left_to_read;
	int buf_pos = 0;

	while (left_to_read > 0)
	{
		int read_bytes = left_to_read;
		if (file_offset + read_bytes > file_iter->size)
			read_bytes = file_iter->size - offset;

		assert(read_bytes > 0);

		in.read(buf + buf_pos, read_bytes);

		assert(read_bytes == in.gcount());

		left_to_read -= read_bytes;
		buf_pos += read_bytes;
		assert(buf_pos >= 0);
		file_offset += read_bytes;

		if (left_to_read > 0)
		{
			++file_iter;
			fs::path path = m_save_path /file_iter->path / file_iter->filename;

			file_offset = 0;
			in.close();
			in.clear();
			in.open(path, std::ios_base::binary);
		}
	}

	return result;
}

void libtorrent::storage::write(const char* buf, int slot, size_type offset, size_type size)
{
	namespace fs = boost::filesystem;

	size_type start = slot * m_torrent_file->piece_length() + offset;

	// find the file iterator and file offset
	size_type file_offset = start;
	std::vector<file>::const_iterator file_iter;

	for (file_iter = m_torrent_file->begin_files();;)
	{
		if (file_offset < file_iter->size)
			break;

		file_offset -= file_iter->size;
		++file_iter;
	}

	fs::ofstream out(
		m_save_path / file_iter->path / file_iter->filename
	  , std::ios_base::in | std::ios_base::binary
	);

	assert(file_offset < file_iter->size);

	out.seekp(std::ios_base::beg, file_offset);

	size_type left_to_write = size;
	size_type slot_size = m_torrent_file->piece_size(slot);

	if (offset + left_to_write > slot_size)
		left_to_write = slot_size - offset;

	assert(left_to_write >= 0);

	int buf_pos = 0;

	while (left_to_write > 0)
	{
		int write_bytes = left_to_write;
		if (file_offset + write_bytes > file_iter->size)
		{
			assert(file_iter->size > file_offset);
			write_bytes = file_iter->size - file_offset;
		}

		assert(buf_pos >= 0);
		assert(write_bytes > 0);
		out.write(buf + buf_pos, write_bytes);

		left_to_write -= write_bytes;
		buf_pos += write_bytes;
		assert(buf_pos >= 0);
		file_offset += write_bytes;
		assert(file_offset <= file_iter->size);

		if (left_to_write > 0)
		{
			++file_iter;

			assert(file_iter != m_torrent_file->end_files());

			fs::path path = m_save_path / file_iter->path / file_iter->filename;

			file_offset = 0;
			out.close();
			out.clear();
			out.open(path, std::ios_base::in | std::ios_base::binary);
		}
	}
}
*/


// -- new storage abstraction -----------------------------------------------

namespace fs = boost::filesystem;

namespace {

	void print_to_log(const std::string& s)
	{
		static std::ofstream log("log.txt");
		log << s;
	}

}

namespace libtorrent {

	struct thread_safe_storage
	{
		thread_safe_storage(std::size_t n)
			: slots(n, false)
		{}

		boost::mutex mutex;
		boost::condition condition;
		std::vector<bool> slots;
	};

	struct slot_lock
	{
		slot_lock(thread_safe_storage& s, int slot_)
			: storage_(s)
			, slot(slot_)
		{
			boost::mutex::scoped_lock lock(storage_.mutex);

			while (storage_.slots[slot])
				storage_.condition.wait(lock);
			storage_.slots[slot] = true;
		}

		~slot_lock()
		{
			storage_.slots[slot] = false;
			storage_.condition.notify_all();
		}

		thread_safe_storage& storage_;
		int slot;
	};

	struct storage::pimpl : thread_safe_storage
	{
		pimpl(const torrent_info& info, const fs::path& path)
			: thread_safe_storage(info.num_pieces())
			, info(info)
			, save_path(path)
		{}

		pimpl(const pimpl& x)
			: thread_safe_storage(x.info.num_pieces())
			, info(x.info)
			, save_path(x.save_path)
		{}

		const torrent_info& info;
		const boost::filesystem::path save_path;
	};

	storage::storage(const torrent_info& info, const fs::path& path)
		: m_pimpl(new pimpl(info, path))
	{
		assert(info.begin_files() != info.end_files());
	}

	storage::~storage() 
	{}

	storage::storage(const storage& other)
		: m_pimpl(new pimpl(*other.m_pimpl))
	{}

	void storage::swap(storage& other)
	{
		std::swap(m_pimpl, other.m_pimpl);
	}

	void storage::operator=(const storage& other)
	{
		storage tmp(other);
		tmp.swap(*this);
	}

	storage::size_type storage::read(
		char* buf
	  , int slot
	  , size_type offset
  	  , size_type size)
	{
		assert(size > 0);

		slot_lock lock(*m_pimpl, slot);
		
		size_type start = slot * m_pimpl->info.piece_length() + offset;

		// find the file iterator and file offset
		size_type file_offset = start;
		std::vector<file>::const_iterator file_iter;

		for (file_iter = m_pimpl->info.begin_files();;)
		{
			if (file_offset < file_iter->size)
				break;

			file_offset -= file_iter->size;
			++file_iter;
		}

		fs::ifstream in(
			m_pimpl->save_path / file_iter->path / file_iter->filename
			, std::ios_base::binary
		);

		assert(file_offset < file_iter->size);

		in.seekg(file_offset);

		assert(size_type(in.tellg()) == file_offset);

		size_type left_to_read = size;
		size_type slot_size = m_pimpl->info.piece_size(slot);

		if (offset + left_to_read > slot_size)
			left_to_read = slot_size - offset;

		assert(left_to_read >= 0);

		int result = left_to_read;
		int buf_pos = 0;

		while (left_to_read > 0)
		{
			int read_bytes = left_to_read;
			if (file_offset + read_bytes > file_iter->size)
				read_bytes = file_iter->size - offset;

			assert(read_bytes > 0);

			in.read(buf + buf_pos, read_bytes);

			int actual_read = in.gcount();
			assert(read_bytes == actual_read);

			left_to_read -= read_bytes;
			buf_pos += read_bytes;
			assert(buf_pos >= 0);
			file_offset += read_bytes;

			if (left_to_read > 0)
			{
				++file_iter;
				fs::path path = m_pimpl->save_path / file_iter->path / file_iter->filename;

				file_offset = 0;
				in.close();
				in.clear();
				in.open(path, std::ios_base::binary);
			}
		}

		return result;
	}

	void storage::write(const char* buf, int slot, size_type offset, size_type size)
	{
		assert(size > 0);

		slot_lock lock(*m_pimpl, slot);

		size_type start = slot * m_pimpl->info.piece_length() + offset;

		// find the file iterator and file offset
		size_type file_offset = start;
		std::vector<file>::const_iterator file_iter;

		for (file_iter = m_pimpl->info.begin_files();;)
		{
			if (file_offset < file_iter->size)
				break;

			file_offset -= file_iter->size;
			++file_iter;
		}

		fs::path path(m_pimpl->save_path / file_iter->path / file_iter->filename);
		fs::ofstream out;

		if (fs::exists(path))
			out.open(path, std::ios_base::binary | std::ios_base::in);
		else
			out.open(path, std::ios_base::binary);

		assert(file_offset < file_iter->size);

		out.seekp(file_offset);

		assert(file_offset == out.tellp());

		size_type left_to_write = size;
		size_type slot_size = m_pimpl->info.piece_size(slot);

		if (offset + left_to_write > slot_size)
			left_to_write = slot_size - offset;

		assert(left_to_write >= 0);

		int buf_pos = 0;

		// TODO
		// handle case when we can't write size bytes.
		while (left_to_write > 0)
		{
			int write_bytes = left_to_write;
			if (file_offset + write_bytes > file_iter->size)
			{
				assert(file_iter->size > file_offset);
				write_bytes = file_iter->size - file_offset;
			}

			assert(buf_pos >= 0);
			assert(write_bytes > 0);
			out.write(buf + buf_pos, write_bytes);

			left_to_write -= write_bytes;
			buf_pos += write_bytes;
			assert(buf_pos >= 0);
			file_offset += write_bytes;
			assert(file_offset <= file_iter->size);

			if (left_to_write > 0)
			{
				++file_iter;

				assert(file_iter != m_pimpl->info.end_files());

				fs::path path = m_pimpl->save_path / file_iter->path / file_iter->filename;

				file_offset = 0;
				out.close();
				out.clear();

				if (fs::exists(path))
					out.open(path, std::ios_base::binary | std::ios_base::in);
				else
					out.open(path, std::ios_base::binary);
			}
		}
	}	

	piece_manager::piece_manager(
		const torrent_info& info
		, const fs::path& save_path)
		: m_storage(info, save_path)
		, m_info(info)
		, m_save_path(save_path)
	{
	}

	piece_manager::size_type piece_manager::read(
		char* buf
		, int piece_index
		, piece_manager::size_type offset
		, piece_manager::size_type size)
	{
		assert(m_piece_to_slot[piece_index] >= 0);
		int slot = m_piece_to_slot[piece_index];
		return m_storage.read(buf, slot, offset, size);
	}

	void piece_manager::write(
		const char* buf
		, int piece_index
		, piece_manager::size_type offset
		, piece_manager::size_type size)
	{
		int slot = slot_for_piece(piece_index);
		m_storage.write(buf, slot, offset, size);
	}

	void piece_manager::check_pieces(
		boost::mutex& mutex
		, detail::piece_checker_data& data
		, std::vector<bool>& pieces)
	{
		// synchronization ------------------------------------------------------
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		// ----------------------------------------------------------------------

		m_piece_to_slot.resize(m_info.num_pieces(), -1);
		m_slot_to_piece.resize(m_info.num_pieces(), -1);
		m_locked_pieces.resize(m_info.num_pieces(), false);

		m_bytes_left = m_info.total_size();

		const std::size_t piece_size = m_info.piece_length();
		const std::size_t last_piece_size = m_info.piece_size(
				m_info.num_pieces() - 1);

		bool changed_file = true;
		fs::ifstream in;

		std::vector<char> piece_data(m_info.piece_length());
		std::size_t piece_offset = 0;

		int current_piece = 0;
		std::size_t bytes_to_read = piece_size;
		std::size_t bytes_current_read = 0;
		std::size_t seek_into_next = 0;
		entry::integer_type filesize = 0;
		entry::integer_type start_of_read = 0;
		entry::integer_type start_of_file = 0;

		{
			boost::mutex::scoped_lock lock(mutex);
			data.progress = 0.f;
		}

		for (torrent_info::file_iterator file_iter = m_info.begin_files(),
			end_iter = m_info.end_files(); 
			file_iter != end_iter;)
		{
			{
				boost::mutex::scoped_lock lock(mutex);

				data.progress = (float)current_piece / m_info.num_pieces();
				if (data.abort)
					return;
			}

			assert(current_piece <= m_info.num_pieces());
			
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
			if (bytes_to_read == m_info.piece_size(current_piece))
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
						m_unallocated_slots.push_back(current_piece);
						++current_piece;
						assert(current_piece <= m_info.num_pieces());
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
		
			// we need to take special actions if this is 
			// the last piece, since that piece might actually 
			// be smaller than piece_size.

			lazy_hash large_digest(&piece_data[0], piece_size);
			lazy_hash small_digest(&piece_data[0], last_piece_size);
			
			const lazy_hash* digest[2] = {
				&large_digest, &small_digest
			};

			int found_piece = -1;

			for (int i = 0; i < m_info.num_pieces(); ++i)
			{
				if (i > current_piece)
					break;

				if (pieces[i])
					continue;

				const sha1_hash& hash = digest[
					i == m_info.num_pieces() - 1]->get();

				if (hash == m_info.hash_for_piece(i))
					found_piece = i;
			}

			if (found_piece != -1)
			{
					m_bytes_left -= m_info.piece_size(found_piece);

					m_piece_to_slot[found_piece] = current_piece;
					m_slot_to_piece[current_piece] = found_piece;
					pieces[found_piece] = true;
			}
			else
			{
				m_slot_to_piece[current_piece] = -2;

				entry::integer_type last_pos =
					m_info.total_size() - 
						m_info.piece_size(
							m_info.num_pieces() - 1);

				// treat the last slot as unallocated space.
				// this means that when we get to the last
				// slot we are either allocating space for
				// the last piece, or the last piece has already
				// been allocated
				if (current_piece == m_info.num_pieces() - 1)
					m_unallocated_slots.push_back(current_piece);
				else
					m_free_slots.push_back(current_piece);
			}

			// done with piece, move on to next
			piece_offset = 0;
			++current_piece;

			bytes_to_read = m_info.piece_size(current_piece);
		}

		std::cout << " m_free_slots: " << m_free_slots.size() << "\n";
		std::cout << " m_unallocated_slots: " << m_unallocated_slots.size() << "\n";
		std::cout << " num pieces: " << m_info.num_pieces() << "\n";

		std::cout << " have_pieces: ";
		print_bitmask(pieces);
		std::cout << "\n";
		std::cout << std::count(pieces.begin(), pieces.end(), true) << "\n";

		check_invariant();
	}

	int piece_manager::slot_for_piece(int piece_index)
	{
		// synchronization ------------------------------------------------------
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		// ----------------------------------------------------------------------

		assert(piece_index >= 0 && piece_index < m_piece_to_slot.size());
		assert(m_piece_to_slot.size() == m_slot_to_piece.size());

		int slot_index = m_piece_to_slot[piece_index];

		if (slot_index != -1)
		{
			assert(slot_index >= 0);
			assert(slot_index < m_slot_to_piece.size());
			return slot_index;
		}

		if (m_free_slots.empty())
		{
			allocate_slots(5);
			assert(!m_free_slots.empty());
		}

		std::vector<int>::iterator iter(
			std::find(
				m_free_slots.begin()
				, m_free_slots.end()
				, piece_index));

		if (iter == m_free_slots.end())
		{
			iter = m_free_slots.end() - 1;

			// special case to make sure we don't use the last slot
			// when we shouldn't, since it's smaller than ordinary slots
			if (*iter == m_info.num_pieces() - 1 && piece_index != *iter)
			{
				if (m_free_slots.size() == 1)
					allocate_slots(5);
				assert(m_free_slots.size() > 1);
				// TODO: assumes that all allocated slots
				// are put at the end of the free_slots vector
				iter = m_free_slots.end() - 1;
			}
		}

		slot_index = *iter;
		m_free_slots.erase(iter);

		assert(m_slot_to_piece[slot_index] == -2);

		m_slot_to_piece[slot_index] = piece_index;
		m_piece_to_slot[piece_index] = slot_index;
	
		// there is another piece already assigned to
		// the slot we are interested in, swap positions
		if (slot_index != piece_index
			&& m_slot_to_piece[piece_index] >= 0)
		{
			std::stringstream s;

			s << "there is another piece at our slot, swapping..";

			s << "\n   piece_index: ";
			s << piece_index;
			s << "\n   slot_index: ";
			s << slot_index;
			s << "\n   piece at our slot: ";
			s << m_slot_to_piece[piece_index];
			s << "\n";

			int piece_at_our_slot = m_slot_to_piece[piece_index];

			print_to_log(s.str());

			debug_log();

			std::vector<char> buf(m_info.piece_length());
			m_storage.read(&buf[0], piece_index, 0, m_info.piece_length());
			m_storage.write(&buf[0], slot_index, 0, m_info.piece_length());

			std::swap(
				m_slot_to_piece[piece_index]
				, m_slot_to_piece[slot_index]);

			std::swap(
				m_piece_to_slot[piece_index]
				, m_piece_to_slot[piece_at_our_slot]);

			slot_index = piece_index;

			debug_log();
		}

		check_invariant();

		return slot_index;
	}

	void piece_manager::allocate_slots(int num_slots)
	{
		// synchronization ------------------------------------------------------
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		// ----------------------------------------------------------------------

		namespace fs = boost::filesystem;
		
		std::cout << "allocating pieces...\n";

		std::vector<int>::iterator iter
			= m_unallocated_slots.begin();
		std::vector<int>::iterator end_iter 
			= m_unallocated_slots.end();

		const size_type piece_size = m_info.piece_length();

		std::vector<char> zeros(piece_size, 0);
		
		for (int i = 0; i < num_slots; ++i, ++iter)
		{
			if (iter == end_iter)
				break;

			int pos = *iter;
			int piece_pos = pos;

			int new_free_slot = pos;

			if (m_piece_to_slot[pos] != -1)
			{
				assert(m_piece_to_slot[pos] >= 0);
				m_storage.read(&zeros[0], m_piece_to_slot[pos], 0, m_info.piece_size(pos));
				new_free_slot = m_piece_to_slot[pos];
				m_slot_to_piece[pos] = pos;
				m_piece_to_slot[pos] = pos;
			}

			m_slot_to_piece[new_free_slot] = -2;
			m_free_slots.push_back(new_free_slot);

			m_storage.write(&zeros[0], pos, 0, m_info.piece_size(pos));
		}

		m_unallocated_slots.erase(m_unallocated_slots.begin(), iter);

		check_invariant();
	}
	
	void piece_manager::check_invariant() const
	{
		// synchronization ------------------------------------------------------
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		// ----------------------------------------------------------------------

		for (int i = 0; i < m_info.num_pieces(); ++i)
		{
			if (m_piece_to_slot[i] != i && m_piece_to_slot[i] >= 0)
				assert(m_slot_to_piece[i] == -1);
		}
	}

	void piece_manager::debug_log() const
	{
		std::stringstream s;

		s << "index\tslot\tpiece\n";

		for (int i = 0; i < m_info.num_pieces(); ++i)
		{
			s << i << "\t" << m_slot_to_piece[i] << "\t";
			s << m_piece_to_slot[i] << "\n";
		}

		s << "---------------------------------\n";

		print_to_log(s.str());
	}
	
} // namespace libtorrent

