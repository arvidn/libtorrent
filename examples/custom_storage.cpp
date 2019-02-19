/*

Copyright (c) 2019, Arvid Norberg
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

#include <cstdlib>
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/fwd.hpp"

#include <iostream>

// -- example begin
struct temp_storage : lt::storage_interface
{
	explicit temp_storage(lt::file_storage const& fs) : lt::storage_interface(fs) {}
	void initialize(lt::storage_error&) override {}
	bool has_any_file(lt::storage_error&) override { return false; }
	void set_file_priority(lt::aux::vector<lt::download_priority_t, lt::file_index_t>&
		, lt::storage_error&) override {}
	int readv(lt::span<lt::iovec_t const> bufs, lt::piece_index_t piece
		, int offset, lt::open_mode_t, lt::storage_error&) override
	{
		auto const i = m_file_data.find(piece);
		if (i == m_file_data.end()) return 0;
		if (int(i->second.size()) <= offset) return 0;
		lt::iovec_t data{ i->second.data() + offset, int(i->second.size() - offset) };
		int ret = 0;
		for (lt::iovec_t const& b : bufs) {
			int const to_copy = std::min(int(b.size()), int(data.size()));
			memcpy(b.data(), data.data(), to_copy);
			data = data.subspan(to_copy);
			ret += to_copy;
			if (data.empty()) break;
		}
		return ret;
	}
	int writev(lt::span<lt::iovec_t const> bufs
		, lt::piece_index_t const piece, int offset, lt::open_mode_t, lt::storage_error&) override
	{
		auto& data = m_file_data[piece];
		int ret = 0;
		for (auto& b : bufs) {
			if (int(data.size()) < offset + b.size()) data.resize(offset + b.size());
			std::memcpy(data.data() + offset, b.data(), b.size());
			offset += int(b.size());
			ret += int(b.size());
		}
		return ret;
	}
	void rename_file(lt::file_index_t, std::string const&, lt::storage_error&) override
	{ assert(false); }
	lt::status_t move_storage(std::string const&
		, lt::move_flags_t, lt::storage_error&) override { return lt::status_t::no_error; }
	bool verify_resume_data(lt::add_torrent_params const&
		, lt::aux::vector<std::string, lt::file_index_t> const&
		, lt::storage_error&) override
	{ return false; }
	void release_files(lt::storage_error&) override {}
	void delete_files(lt::remove_flags_t, lt::storage_error&) override {}

	std::map<lt::piece_index_t, std::vector<char>> m_file_data;
};

lt::storage_interface* temp_storage_constructor(lt::storage_params const& params, lt::file_pool&)
{
	return new temp_storage(params.files);
}

// -- example end

int main(int argc, char* argv[]) try
{
	if (argc != 2) {
		std::cerr << "usage: ./custom_storage torrent-file\n"
			"to stop the client, press return.\n";
		return 1;
	}

	lt::session s;
	lt::add_torrent_params p;
	p.storage = temp_storage_constructor;
	p.save_path = "./";
	p.ti = std::make_shared<lt::torrent_info>(argv[1]);
	s.add_torrent(p);

	// wait for the user to end
	char a;
	int ret = std::scanf("%c\n", &a);
	(void)ret; // ignore
	return 0;
}
catch (std::exception const& e) {
	std::cerr << "ERROR: " << e.what() << "\n";
}

