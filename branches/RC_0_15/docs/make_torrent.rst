=================
creating torrents
=================

:Author: Arvid Norberg, arvid@rasterbar.com
:Version: 0.15.10

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
		void add_files(file_storage& fs, boost::filesystem::path const& path, Pred p
			, boost::uint32_t flags = 0);
		template <class Pred>
		void add_files(file_storage& fs, boost::filesystem::wpath const& path, Pred p
			, boost::uint32_t flags = 0);

		void add_files(file_storage& fs, boost::filesystem::path const& path
			, boost::uint32_t flags = 0);
		void add_files(file_storage& fs, boost::filesystem::wpath const& path
			, boost::uint32_t flags = 0);

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

The ``flags`` argument should be the same as the flags passed to the `create_torrent`_
constructor.

set_piece_hashes()
==================

	::

		template <class Fun>
		void set_piece_hashes(create_torrent& t, boost::filesystem::path const& p, Fun f);
		template <class Fun>
		void set_piece_hashes(create_torrent& t, boost::filesystem::wpath const& p, Fun f);
		template <class Fun>
		void set_piece_hashes(create_torrent& t, boost::filesystem::path const& p, Fun f
			, error_code& ec);
		template <class Fun>
		void set_piece_hashes(create_torrent& t, boost::filesystem::wpath const& p, Fun f
			, error_code& ec);

		void set_piece_hashes(create_torrent& t, boost::filesystem::path const& p);
		void set_piece_hashes(create_torrent& t, boost::filesystem::wpath const& p);
		void set_piece_hashes(create_torrent& t, boost::filesystem::path const& p
			, error_code& ec);
		void set_piece_hashes(create_torrent& t, boost::filesystem::wpath const& p
			, error_code& ec);

This function will assume that the files added to the torrent file exists at path
``p``, read those files and hash the content and set the hashes in the ``create_torrent``
object. The optional function ``f`` is called in between every hash that is set. ``f``
must have the following signature::

	void Fun(int);

The overloads that don't take an ``error_code&`` may throw an exception in case of a
file error, the other overloads sets the error code to reflect the error, if any.

file_storage
============

The ``file_storage`` class represents a file list and the piece
size. Everything necessary to interpret a regular bittorrent storage
file structure. Its synopsis::

	class file_storage
	{
	public:

		bool is_valid() const;

		enum flags_t
		{
			pad_file = 1,
			attribute_hidden = 2,
			attribute_executable = 4
		};

		void add_file(file_entry const& e);
		void add_file(fs::path const& p, size_type size, int flags = 0);
		void add_file(fs::wpath const& p, size_type size, int flags = 0);
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

add_file()
----------

	::

		void add_file(file_entry const& e);
		void add_file(fs::path const& p, size_type size, int flags = 0);
		void add_file(fs::wpath const& p, size_type size, int flags = 0);

Adds a file to the file storage. The ``flags`` argument sets attributes on the file.
The file attributes is an extension and may not work in all bittorrent clients.
The possible arreibutes are::

	pad_file
	attribute_hidden
	attribute_executable

add_file
--------

	::

		void add_file(file_entry const& e);
		void add_file(fs::path const& p, size_type size);

Adds a file to the file storage. If more files than one are added,
certain restrictions to their paths apply. In a multi-file file
storage (torrent), all files must share the same root directory.

That is, the first path element of all files must be the same.
This shared path element is also set to the name of the torrent. It
can be changed by calling ``set_name``.

The built in functions to traverse a directory to add files will
make sure this requirement is fulfilled.


create_torrent
==============

The ``create_torrent`` class has the following synopsis::


	struct create_torrent
	{
		enum {
			optimize = 1
			, merkle = 2
			, modification_time = 4
			, symlink = 8
		};
		create_torrent(file_storage& fs, int piece_size = 0, int pad_size_limit = -1
			, int flags = optimize);
		create_torrent(torrent_info const& ti);

		entry generate() const;

		file_storage const& files() const;

		void set_comment(char const* str);
		void set_creator(char const* str);
		void set_hash(int index, sha1_hash const& h);
		void add_url_seed(std::string const& url);
		void add_http_seed(std::string const& url);
		void add_node(std::pair<std::string, int> const& node);
		void add_tracker(std::string const& url, int tier = 0);
		void set_priv(bool p);

		int num_pieces() const;
		int piece_length() const;
		int piece_size(int i) const;
		bool priv() const;
	};

create_torrent()
----------------

	::

		enum {
			optimize = 1
			, merkle = 2
			, modification_time = 4
			, symlink = 8
		};
		create_torrent(file_storage& fs, int piece_size = 0, int pad_size_limit = -1
			, int flags = optimize);
		create_torrent(torrent_info const& ti);

The ``piece_size`` is the size of each piece in bytes. It must
be a multiple of 16 kiB. If a piece size of 0 is specified, a
piece_size will be calculated such that the torrent file is roughly 40 kB.

If a ``pad_size_limit`` is specified (other than -1), any file larger than
the specified number of bytes will be preceeded by a pad file to align it
with the start of a piece. The pad_file_limit is ignored unless the
``optimize`` flag is passed. Typically it doesn't make sense to set this
any lower than 4kiB.

The overload that takes a ``torrent_info`` object will make a verbatim
copy of its info dictionary (to preserve the info-hash). The copy of
the info dictionary will be used by ``generate()``. This means
that none of the member functions of create_torrent that affects
the content of the info dictionary (such as ``set_hash()``), will
have any affect.

The ``flags`` arguments specifies options for the torrent creation. It can
be any combination of the following flags:

optimize
	This will insert pad files to align the files to piece boundaries, for
	optimized disk-I/O.

merkle
	This will create a merkle hash tree torrent. A merkle torrent cannot
	be opened in clients that don't specifically support merkle torrents.
	The benefit is that the resulting torrent file will be much smaller and
	not grow with more pieces. When this option is specified, it is
	recommended to have a fairly small piece size, say 64 kiB.
	When creating merkle torrents, the full hash tree is also generated
	and should be saved off separately. It is accessed through  the 
	``merkle_tree()`` function.

modification_time
	This will include the file modification time as part of the torrent.
	This is not enabled by default, as it might cause problems when you
	create a torrent from separate files with the same content, hoping to
	yield the same info-hash. If the files have different modification times,
	with this option enabled, you would get different info-hashes for the
	files.

symlink
	If this flag is set, files that are symlinks get a symlink attribute
	set on them and their data will not be included in the torrent. This
	is useful if you need to reconstruct a file hierarchy which contains
	symlinks.

generate()
----------

	::

		entry generate() const;

This function will generate the .torrent file as a bencode tree. In order to
generate the flat file, use the bencode() function.

It may be useful to add custom entries to the torrent file before bencoding it
and saving it to disk.

If anything goes wrong during torrent generation, this function will return
an empty ``entry`` structure. You can test for this condition by querying the
type of the entry::

	file_storage fs;
	// add file ...
	create_torrent t(fs);
	// add trackers and piece hashes ...
	e = t.generate();

	if (e.type() == entry::undefined_t)
	{
		// something went wrong
	}

For instance, you cannot generate a torrent with 0 files in it. If you don't add
any files to the ``file_storage``, torrent generation will fail.

set_comment()
-------------

	::

		void set_comment(char const* str);

Sets the comment for the torrent. The string ``str`` should be utf-8 encoded.
The comment in a torrent file is optional.

set_creator()
-------------

	::

		void set_creator(char const* str);

Sets the creator of the torrent. The string ``str`` should be utf-8 encoded.
This is optional.

set_hash()
----------

	::

		void set_hash(int index, sha1_hash const& h);

This sets the SHA-1 hash for the specified piece (``index``). You are required
to set the hash for every piece in the torrent before generating it. If you have
the files on disk, you can use the high level convenience function to do this.
See `set_piece_hashes()`_.

add_url_seed() add_http_seed()
------------------------------

	::

		void add_url_seed(std::string const& url);
		void add_http_seed(std::string const& url);

This adds a url seed to the torrent. You can have any number of url seeds. For a
single file torrent, this should be an HTTP url, pointing to a file with identical
content as the file of the torrent. For a multi-file torrent, it should point to
a directory containing a directory with the same name as this torrent, and all the
files of the torrent in it.

The second function, ``add_http_seed()`` adds an HTTP seed instead.

add_node()
----------

	::

		void add_node(std::pair<std::string, int> const& node);

This adds a DHT node to the torrent. This especially useful if you're creating a
tracker less torrent. It can be used by clients to bootstrap their DHT node from.
The node is a hostname and a port number where there is a DHT node running.
You can have any number of DHT nodes in a torrent.

add_tracker()
-------------

	::

		void add_tracker(std::string const& url, int tier = 0);

Adds a tracker to the torrent. This is not strictly required, but most torrents
use a tracker as their main source of peers. The url should be an http:// or udp://
url to a machine running a bittorrent tracker that accepts announces for this torrent's
info-hash. The tier is the fallback priority of the tracker. All trackers with tier 0 are
tried first (in any order). If all fail, trackers with tier 1 are tried. If all of those
fail, trackers with tier 2 are tried, and so on.

set_priv() priv()
-----------------

	::

		void set_priv(bool p);
		bool priv() const;

Sets and queries the private flag of the torrent.

merkle_tree()
-------------

	::

		std::vector<sha1_hash> const& merkle_tree() const;

This function returns the merkle hash tree, if the torrent was created as a merkle
torrent. The tree is created by ``generate()`` and won't be valid until that function
has been called. When creating a merkle tree torrent, the actual tree itself has to
be saved off separately and fed into libtorrent the first time you start seeding it,
through the ``torrent_info::set_merkle_tree()`` function. From that point onwards, the
tree will be saved in the resume data.



