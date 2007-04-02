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

#include <iostream>
#include <fstream>
#include <iterator>
#include <iomanip>

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent_info.hpp"


int main(int argc, char* argv[])
{
	using namespace libtorrent;

	if (argc != 2)
	{
		std::cerr << "usage: dump_torrent torrent-file\n";
		return 1;
	}

	boost::filesystem::path::default_name_check(boost::filesystem::no_check);

	try
	{
		std::ifstream in(argv[1], std::ios_base::binary);
		in.unsetf(std::ios_base::skipws);
		entry e = bdecode(std::istream_iterator<char>(in), std::istream_iterator<char>());

		std::cout << "\n\n----- raw info -----\n\n";
		e.print(std::cout);

		torrent_info t(e);
	
		// print info about torrent
		std::cout << "\n\n----- torrent file info -----\n\n";
		std::cout << "nodes:\n";
		typedef std::vector<std::pair<std::string, int> > node_vec;
		node_vec const& nodes = t.nodes();
		for (node_vec::const_iterator i = nodes.begin(), end(nodes.end());
			i != end; ++i)
		{
			std::cout << i->first << ":" << i->second << "\n";
		}
		std::cout << "trackers:\n";
		for (std::vector<announce_entry>::const_iterator i = t.trackers().begin();
			i != t.trackers().end(); ++i)
		{
			std::cout << i->tier << ": " << i->url << "\n";
		}

		std::cout << "number of pieces: " << t.num_pieces() << "\n";
		std::cout << "piece length: " << t.piece_length() << "\n";
		std::cout << "info hash: " << t.info_hash() << "\n";
		std::cout << "comment: " << t.comment() << "\n";
		std::cout << "created by: " << t.creator() << "\n";
		std::cout << "files:\n";
		for (torrent_info::file_iterator i = t.begin_files();
			i != t.end_files(); ++i)
		{
			std::cout << "  " << std::setw(11) << i->size
			<< " " << i->path.string() << "\n";
		}
		
	}
	catch (std::exception& e)
	{
  		std::cout << e.what() << "\n";
	}

	return 0;
}

