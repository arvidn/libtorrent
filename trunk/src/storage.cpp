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
#include <boost/thread/mutex.hpp>

#include "libtorrent/storage.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/session.hpp"

#if defined(_MSC_VER) && _MSV_CER < 1300
#define for if (false) {} else for
#endif

using namespace libtorrent;


// TODO: when a piece_file is opened, a seek offset should be
// accepted as an argument, this way we may avoid opening a
// file in vain if we're about to seek forward anyway

void libtorrent::piece_file::open(storage* s, int index, open_mode o, int seek_offset)
{
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
}

int libtorrent::piece_file::read(char* buf, int size)
{
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
}

void libtorrent::piece_file::write(const char* buf, int size)
{
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
}

void libtorrent::piece_file::seek_forward(int step)
{
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
/*
// reads the piece with the given index from disk
// and writes it into the buffer. The buffer must have
// room for at least m_piece_length bytes.
// the return value is the size of the piece that was read
// and the number of bytes written to the buffer
int libtorrent::torrent::read_piece(unsigned int index, char* buffer) const
{
	assert(index < m_torrent_file.num_pieces() && "internal error");
	int piece_byte_offset = index * m_torrent_file.piece_length();
	entry::integer_type file_byte_offset = 0;
	std::vector<file>::const_iterator i;
	for (i = m_torrent_file.begin(); i != m_torrent_file.end(); ++i)
	{
		if (file_byte_offset + i->size > piece_byte_offset) break;
		file_byte_offset += i->size;
	}
	
	// i now refers to the first file this piece is stored in
	int left_to_read = m_torrent_file.piece_size(index);
	const int piece_size = m_torrent_file.piece_size(index);
	while (left_to_read > 0)
	{
		assert(i != m_torrent_file.end() && "internal error, please report!");
		boost::filesystem::path path = m_save_path;
		path /= i->path;
		path /= i->filename;
		std::ifstream f(path.native_file_string().c_str(), std::ios_base::binary);
		f.seekg(piece_byte_offset - file_byte_offset, std::ios_base::beg);
		f.read(buffer + piece_size - left_to_read, left_to_read);
		int read = f.gcount();
		left_to_read -= read;
		piece_byte_offset += read;
		file_byte_offset += i->size;
		++i;
	}
	return m_torrent_file.piece_size(index);
}

void libtorrent::torrent::write_piece(unsigned int index, const char* buffer) const
{
	const int piece_byte_offset = index * m_torrent_file.piece_length();
	entry::integer_type file_byte_offset = 0;
	std::vector<file>::const_iterator i;
	for (i = m_torrent_file.begin(); i != m_torrent_file.end(); ++i)
	{
		if (file_byte_offset + i->size > piece_byte_offset) break;
		file_byte_offset += i->size;
	}
	assert(i != m_torrent_file.end() && "internal error, please report!");

	// i now refers to the first file this piece is stored in

	int piece_size = m_torrent_file.piece_size(index);
    
	int written = 0;
	while (written < piece_size)
	{
		boost::filesystem::path path = m_save_path;
		path /= i->path;
		path /= i->filename;
		std::fstream f(path.native_file_string().c_str(), std::ios_base::binary | std::ios_base::in | std::ios_base::out);
		f.seekp(piece_byte_offset + written - file_byte_offset, std::ios_base::beg);

		int this_write = piece_size - written;
		// check if this file is big enough to store the entire piece
		if ( this_write > i->size - piece_byte_offset - written + file_byte_offset)
			this_write = i->size - piece_byte_offset - written + file_byte_offset;

		f.write(buffer + written, this_write);
		written += this_write;
		file_byte_offset += i->size;
		++i;
	}
}
*/
