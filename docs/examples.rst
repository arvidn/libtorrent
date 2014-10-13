===================
libtorrent Examples
===================

:Author: Arvid Norberg, arvid@libtorrent.org

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

examples
========

Except for the example programs in this manual, there's also a bigger example
of a (little bit) more complete client, ``client_test``. There are separate
instructions for how to use it here__ if you'd like to try it. Note that building
``client_test`` also requires boost.regex and boost.program_options library.

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
	#include "libtorrent/lazy_entry.hpp"
	#include <boost/filesystem/operations.hpp>
	
	
	int main(int argc, char* argv[])
	{
		using namespace libtorrent;
		using namespace boost::filesystem;
	
		if (argc != 2)
		{
			std::cerr << "usage: dump_torrent torrent-file\n";
			return 1;
		}
	#if BOOST_VERSION < 103400
		boost::filesystem::path::default_name_check(boost::filesystem::no_check);
	#endif
	
	#ifndef BOOST_NO_EXCEPTIONS
		try
		{
	#endif
	
			int size = file_size(argv[1]);
			if (size > 10 * 1000000)
			{
				std::cerr << "file too big (" << size << "), aborting\n";
				return 1;
			}
			std::vector<char> buf(size);
			std::ifstream(argv[1], std::ios_base::binary).read(&buf[0], size);
			lazy_entry e;
			int ret = lazy_bdecode(&buf[0], &buf[0] + buf.size(), e);
	
			if (ret != 0)
			{
				std::cerr << "invalid bencoding: " << ret << std::endl;
				return 1;
			}
	
			std::cout << "\n\n----- raw info -----\n\n";
			std::cout << e << std::endl;
		
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
			int index = 0;
			for (torrent_info::file_iterator i = t.begin_files();
				i != t.end_files(); ++i, ++index)
			{
				int first = t.map_file(index, 0, 1).piece;
				int last = t.map_file(index, i->size - 1, 1).piece;
				std::cout << "  " << std::setw(11) << i->size
					<< " " << i->path.string() << "[ " << first << ", "
					<< last << " ]\n";
			}
	
	#ifndef BOOST_NO_EXCEPTIONS
		}
		catch (std::exception& e)
		{
	  		std::cout << e.what() << "\n";
		}
	#endif
	
		return 0;
	}

simple client
-------------

This is a simple client. It doesn't have much output to keep it simple::

	int main(int argc, char* argv[])
	{
		using namespace libtorrent;
	#if BOOST_VERSION < 103400
		namespace fs = boost::filesystem;
		fs::path::default_name_check(fs::no_check);
	#endif
	
	if (argc != 2)
	{
		std::cerr << "usage: ./simple_client torrent-file\n"
			"to stop the client, press return.\n";
		return 1;
	}
	
	#ifndef BOOST_NO_EXCEPTIONS
		try
	#endif
		{
			session s;
			s.listen_on(std::make_pair(6881, 6889));
			add_torrent_params p;
			p.save_path = "./";
			p.ti = new torrent_info(argv[1]);
			s.add_torrent(p);
	
			// wait for the user to end
			char a;
			std::cin.unsetf(std::ios_base::skipws);
			std::cin >> a;
		}
	#ifndef BOOST_NO_EXCEPTIONS
		catch (std::exception& e)
		{
	  		std::cout << e.what() << "\n";
		}
	#endif
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
	#include "libtorrent/create_torrent.hpp"
	
	#include <boost/filesystem/operations.hpp>
	#include <boost/filesystem/path.hpp>
	#include <boost/filesystem/fstream.hpp>
	#include <boost/bind.hpp>
	
	using namespace boost::filesystem;
	using namespace libtorrent;
	
	// do not include files and folders whose
	// name starts with a .
	bool file_filter(boost::filesystem::path const& filename)
	{
		if (filename.leaf()[0] == '.') return false;
		std::cerr << filename << std::endl;
		return true;
	}
	
	void print_progress(int i, int num)
	{
		std::cerr << "\r" << (i+1) << "/" << num;
	}
	
	int main(int argc, char* argv[])
	{
		using namespace libtorrent;
		using namespace boost::filesystem;
	
		int piece_size = 256 * 1024;
		char const* creator_str = "libtorrent";
	
		path::default_name_check(no_check);
	
		if (argc != 4 && argc != 5)
		{
			std::cerr << "usage: make_torrent <output torrent-file> "
			"<announce url> <file or directory to create torrent from> "
			"[url-seed]\n";
		return 1;
	}
	
	#ifndef BOOST_NO_EXCEPTIONS
		try
		{
	#endif
			file_storage fs;
			file_pool fp;
			path full_path = complete(path(argv[3]));
	
			add_files(fs, full_path, file_filter);
	
			create_torrent t(fs, piece_size);
			t.add_tracker(argv[2]);
			set_piece_hashes(t, full_path.branch_path()
				, boost::bind(&print_progress, _1, t.num_pieces()));
			std::cerr << std::endl;
			t.set_creator(creator_str);
	
			if (argc == 5) t.add_url_seed(argv[4]);
	
			// create the torrent and print it to out
			ofstream out(complete(path(argv[1])), std::ios_base::binary);
			bencode(std::ostream_iterator<char>(out), t.generate());
	#ifndef BOOST_NO_EXCEPTIONS
		}
		catch (std::exception& e)
		{
			std::cerr << e.what() << "\n";
		}
	#endif
	
		return 0;
	}
	
