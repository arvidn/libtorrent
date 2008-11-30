=================
creating torrents
=================

:Author: Arvid Norberg, arvid@rasterbar.com

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

overview
========

This section describes the functions and classes that are used
to create torrent files. It is a layered API with low level classes
and higher level convenience functions. A torrent is created in 4
steps:

1. first the files that will be part of the torrent are determined.
2. the torrent properties are set, such as tracker url, web seeds,
   DHT nodes etc.
3. Read through all the files in the torrent, SHA-1 all the data
   and set the piece hashes.
4. The torrent is bencoded into a file or buffer.

If there are a lot of files and or deep directoy hierarchies to
traverse, step one can be time consuming.

Typically step 3 is by far the most time consuming step, since it
requires to read all the bytes from all the files in the torrent.

All of these classes and functions are declared by including
``libtorrent/create_torrent.hpp``.

high level example
==================

::

	file_storage fs;

	// recursively adds files in directories
	add_files(fs, "./my_torrent");
	
	create_torrent t(fs);
	t.add_tracker("http://my.tracker.com/announce");
	t.set_creator("libtorrent example");

	// reads the files and calculates the hashes
	set_piece_hashes(t, ".");

	ofstream out("my_torrent.torrent", std::ios_base::binary);
	bencode(std::ostream_iterator<char>(out), t.generate());

add_files
=========

	::
	
		template <class Pred>
		void add_files(file_storage& fs, boost::filesystem::path const& path, Pred p);
		template <class Pred>
		void add_files(file_storage& fs, boost::filesystem::wpath const& path, Pred p);

		void add_files(file_storage& fs, boost::filesystem::path const& path);
		void add_files(file_storage& fs, boost::filesystem::wpath const& path);

Adds the file specified by ``path`` to the ``file_storage`` object. In case ``path``
refers to a diretory, files will be added recursively from the directory.

If specified, the predicate ``p`` is called once for every file and directory that
is encountered. files for which ``p`` returns true are added, and directories for
which ``p`` returns true are traversed. ``p`` must have the following signature::

	bool Pred(boost::filesystem::path const& p);

and for the wpath version::

	bool Pred(boost::filesystem::wpath const& p);

The path that is passed in to the predicate is the full path of the file or
directory. If no predicate is specified, all files are added, and all directories
are traveresed.

The ".." directory is never traversed.

set_piece_hashes()
==================

	::

		template <class Fun>
		void set_piece_hashes(create_torrent& t, boost::filesystem::path const& p, Fun f);
		template <class Fun>
		void set_piece_hashes(create_torrent& t, boost::filesystem::wpath const& p, Fun f);

		void set_piece_hashes(create_torrent& t, boost::filesystem::path const& p);
		void set_piece_hashes(create_torrent& t, boost::filesystem::wpath const& p);

This function will assume that the files added to the torrent file exists at path
``p``, read those files and hash the content and set the hashes in the ``create_torrent``
object. The optional function ``f`` is called in between every hash that is set. ``f``
must have the following signature::

	void Fun(int);

file_storage
============

The ``file_storage`` class represents a file list and the piece
size. Everything necessary to interpret a regular bittorrent storage
file structure. Its synopsis::

	class file_storage
	{
	public:

		bool is_valid() const;

		void add_file(file_entry const& e);
		void add_file(fs::path const& p, size_type size);
		void add_file(fs::wpath const& p, size_type size);
		void rename_file(int index, std::string const& new_filename);
		void rename_file(int index, std::wstring const& new_filename);

		std::vector<file_slice> map_block(int piece, size_type offset
			, int size) const;
		peer_request map_file(int file, size_type offset, int size) const;
		
		typedef std::vector<file_entry>::const_iterator iterator;
		typedef std::vector<file_entry>::const_reverse_iterator reverse_iterator;

		iterator begin() const;
		iterator end() const;
		reverse_iterator rbegin();
		reverse_iterator rend() const;
		int num_files() const;

		file_entry const& at(int index) const;
		
		size_type total_size() const;
		void set_num_pieces(int n);
		int num_pieces() const;
		void set_piece_length(int l);
		int piece_length() const;
		int piece_size(int index) const;

		void set_name(std::string const& n);
		void set_name(std::wstring const& n);
		const std::string& name() const;

		void swap(file_storage& ti);
	}


create_torrent
==============

The ``create_torrent`` class has the following synopsis::


	struct create_torrent
	{
		create_torrent(file_storage& fs, int piece_size);
		create_torrent(file_storage& fs);
		create_torrent(torrent_info const& ti);

		entry generate() const;

		file_storage const& files() const;

		void set_comment(char const* str);
		void set_creator(char const* str);
		void set_hash(int index, sha1_hash const& h);
		void add_url_seed(std::string const& url);
		void add_node(std::pair<std::string, int> const& node);
		void add_tracker(std::string const& url, int tier = 0);

		int num_pieces() const;
		int piece_length() const;
		int piece_size(int i) const;
	};

create_torrent()
----------------

	::

		create_torrent(file_storage& fs, int piece_size);
		create_torrent(file_storage& fs);
		create_torrent(torrent_info const& ti);

The contrstructor that does not take a piece_size will calculate
a piece size such that the torrent file is roughly 40 kB.

The overlad that takes a ``torrent_info`` object will make a verbatim
copy of its info dictionary (to preserve the info-hash). The copy of
the info dictionary will be used by ``generate()``. This means
that none of the member functions of create_torrent that affects
the content of the info dictionary (such as ``set_hash()``), will not
have any affect.

generate()
----------

	::

		entry generate() const;

This function will generate the .torrent file as a bencode tree. In order to
generate the flat file, use the bencode() function.

It may be useful to add custom entries to the torrent file before bencoding it
and saving it to disk.


