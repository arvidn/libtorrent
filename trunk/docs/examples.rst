===================
libtorrent Examples
===================

:Author: Arvid Norberg, arvid@rasterbar.com

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

examples
========

Except for the example programs in this manual, there's also a bigger example
of a (little bit) more complete client, ``client_test``. There are separate
instructions for how to use it here__ if you'd like to try it.

__ client_test.html

dump_torrent
------------

This is an example of a program that will take a torrent-file as a parameter and
print information about it to std out::


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

		try
		{
			std::ifstream in(argv[1], std::ios_base::binary);
			in.unsetf(std::ios_base::skipws);
			entry e = bdecode(std::istream_iterator<char>(in)
				, std::istream_iterator<char>());

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
			for (std::vector<announce_entry>::const_iterator i
				= t.trackers().begin(); i != t.trackers().end(); ++i)
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


simple client
-------------

This is a simple client. It doesn't have much output to keep it simple::

	#include <iostream>
	#include <fstream>
	#include <iterator>
	#include <exception>

	#include <boost/format.hpp>
	#include <boost/date_time/posix_time/posix_time.hpp>

	#include "libtorrent/entry.hpp"
	#include "libtorrent/bencode.hpp"
	#include "libtorrent/session.hpp"

	int main(int argc, char* argv[])
	{
		using namespace libtorrent;
	
		if (argc != 2)
		{
			std::cerr << "usage: ./simple_cient torrent-file\n"
				"to stop the client, press return.\n";
			return 1;
		}

		try
		{
			session s;
			s.listen_on(std::make_pair(6881, 6889));
	
			std::ifstream in(argv[1], std::ios_base::binary);
			in.unsetf(std::ios_base::skipws);
			entry e = bdecode(std::istream_iterator<char>(in)
				, std::istream_iterator<char>());
			s.add_torrent(torrent_info(e), "");
				
			// wait for the user to end
			char a;
			std::cin.unsetf(std::ios_base::skipws);
			std::cin >> a;
		}
		catch (std::exception& e)
		{
	  		std::cout << e.what() << "\n";
		}
		return 0;
	}

make_torrent
------------

Shows how to create a torrent from a directory tree::

	#include <iostream>
	#include <fstream>
	#include <iterator>
	#include <iomanip>

	#include "libtorrent/entry.hpp"
	#include "libtorrent/bencode.hpp"
	#include "libtorrent/torrent_info.hpp"
	#include "libtorrent/file.hpp"
	#include "libtorrent/storage.hpp"
	#include "libtorrent/hasher.hpp"

	#include <boost/filesystem/operations.hpp>
	#include <boost/filesystem/path.hpp>
	#include <boost/filesystem/fstream.hpp>

	using namespace boost::filesystem;
	using namespace libtorrent;

	void add_files(torrent_info& t, path const& p, path const& l)
	{
		path f(p / l);
		if (is_directory(f))
		{
			for (directory_iterator i(f), end; i != end; ++i)
				add_files(t, p, l / i->leaf());
		}
		else
		{
			std::cerr << "adding \"" << l.string() << "\"\n";
			file fi(f, file::in);
			fi.seek(0, file::end);
			libtorrent::size_type size = fi.tell();
			t.add_file(l, size);
		}
	}

	int main(int argc, char* argv[])
	{
		using namespace libtorrent;
		using namespace boost::filesystem;

		if (argc != 4)
		{
			std::cerr << "usage: make_torrent <output torrent-file> "
				"<announce url> <file or directory to create torrent from>\n";
			return 1;
		}

		boost::filesystem::path::default_name_check(native);

		try
		{
			torrent_info t;
			path full_path = initial_path() / path(argv[3]);
			ofstream out(initial_path() / path(argv[1]), std::ios_base::binary);

			int piece_size = 256 * 1024;
			char const* creator_str = "libtorrent";

			add_files(t, full_path.branch_path(), full_path.leaf());
			t.set_piece_size(piece_size);

			storage st(t, full_path.branch_path());
			t.add_tracker(argv[2]);

			// calculate the hash for all pieces
			int num = t.num_pieces();
			std::vector<char> buf(piece_size);
			for (int i = 0; i < num; ++i)
			{
				st.read(&buf[0], i, 0, t.piece_size(i));
				hasher h(&buf[0], t.piece_size(i));
				t.set_hash(i, h.final());
				std::cerr << (i+1) << "/" << num << "\r";
			}

			t.set_creator(creator_str);

			// create the torrent and print it to out
			entry e = t.create_torrent();
			libtorrent::bencode(std::ostream_iterator<char>(out), e);
		}
		catch (std::exception& e)
		{
			std::cerr << e.what() << "\n";
		}

		return 0;
	}


