// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/torrent_handle.hpp>
#include <boost/python.hpp>
#include "gil.hpp"

using namespace boost::python;
using namespace libtorrent;

namespace
{

  std::vector<announce_entry>::const_iterator begin_trackers(torrent_handle& i)
  {
      allow_threading_guard guard;
      return i.trackers().begin();
  }


  std::vector<announce_entry>::const_iterator end_trackers(torrent_handle& i)
  {
      allow_threading_guard guard;
      return i.trackers().end();
  }

} // namespace unnamed

list file_progress(torrent_handle& handle)
{
    std::vector<float> p;

    {
        allow_threading_guard guard;
        p.reserve(handle.get_torrent_info().num_files());
        handle.file_progress(p);
    }

    list result;

    for (std::vector<float>::iterator i(p.begin()), e(p.end()); i != e; ++i)
        result.append(*i);

    return result;
}

list get_peer_info(torrent_handle const& handle)
{
    std::vector<peer_info> pi;

    {
        allow_threading_guard guard;
        handle.get_peer_info(pi);
    }

    list result;

    for (std::vector<peer_info>::iterator i = pi.begin(); i != pi.end(); ++i)
    {
        result.append(*i);
    }

    return result;
}


void prioritize_files(torrent_handle& info, object o)
{

	std::vector<int> result;
  try
  {
		object iter_obj = object( handle<>( PyObject_GetIter( o.ptr() ) ));
		while( 1 )
		{
			object obj = extract<object>( iter_obj.attr( "next" )() );
      result.push_back(extract<int const>( obj ));
		}
	}
  catch( error_already_set )
	{
		PyErr_Clear();
  	info.prioritize_files(result);
		return;
	}

}


void replace_trackers(torrent_handle& info, object trackers)
{
    object iter(trackers.attr("__iter__")());

    std::vector<announce_entry> result;

    for (;;)
    {
        handle<> entry(allow_null(PyIter_Next(iter.ptr())));

        if (entry == handle<>())
            break;

        result.push_back(extract<announce_entry const&>(object(entry)));
    }

    allow_threading_guard guard;
    info.replace_trackers(result);
}

list get_download_queue(torrent_handle& handle)
{
	list ret;

	std::vector<partial_piece_info> downloading;

	{
		allow_threading_guard guard;
		handle.get_download_queue(downloading);
	}

	for (std::vector<partial_piece_info>::iterator i = downloading.begin()
		, end(downloading.end()); i != end; ++i)
	{
		dict partial_piece;
		partial_piece["piece_index"] = i->piece_index;
		partial_piece["blocks_in_piece"] = i->blocks_in_piece;
		list block_list;
		for (int k = 0; k < i->blocks_in_piece; ++k)
		{
			dict block_info;
			block_info["state"] = i->blocks[k].state;
			block_info["num_peers"] = i->blocks[k].num_peers;
			block_info["bytes_progress"] = i->blocks[k].bytes_progress;
			block_info["block_size"] = i->blocks[k].block_size;
//			block_info["peer"] = i->info[k].peer;
			block_list.append(block_info);
		}
		partial_piece["blocks"] = block_list;

		ret.append(partial_piece);
	}

	return ret;
}

void bind_torrent_handle()
{
    void (torrent_handle::*force_reannounce0)() const = &torrent_handle::force_reannounce;
    void (torrent_handle::*force_reannounce1)(boost::posix_time::time_duration) const 
        = &torrent_handle::force_reannounce;

    int (torrent_handle::*piece_priority0)(int) const = &torrent_handle::piece_priority;
    void (torrent_handle::*piece_priority1)(int, int) const = &torrent_handle::piece_priority;
    
    return_value_policy<copy_const_reference> copy;

#define _ allow_threads

    class_<torrent_handle>("torrent_handle")
        .def("status", _(&torrent_handle::status))
        .def("torrent_info", _(&torrent_handle::get_torrent_info), return_internal_reference<>())
        .def("is_valid", _(&torrent_handle::is_valid))
        .def("write_resume_data", _(&torrent_handle::write_resume_data))
        .def("force_reannounce", _(force_reannounce0))
        .def("force_reannounce", _(force_reannounce1))
        .def("set_tracker_login", _(&torrent_handle::set_tracker_login))
        .def("add_url_seed", _(&torrent_handle::add_url_seed))
        .def("set_ratio", _(&torrent_handle::set_ratio))
        .def("set_max_uploads", _(&torrent_handle::set_max_uploads))
        .def("set_max_connections", _(&torrent_handle::set_max_connections))
        .def("set_upload_limit", _(&torrent_handle::set_upload_limit))
        .def("set_download_limit", _(&torrent_handle::set_download_limit))
        .def("set_sequenced_download_threshold", _(&torrent_handle::set_sequenced_download_threshold))
        .def("pause", _(&torrent_handle::pause))
        .def("resume", _(&torrent_handle::resume))
        .def("is_paused", _(&torrent_handle::is_paused))
        .def("is_seed", _(&torrent_handle::is_seed))
        .def("filter_piece", _(&torrent_handle::filter_piece))
        .def("piece_priority", _(piece_priority0))
        .def("piece_priority", _(piece_priority1))
        .def("is_piece_filtered", _(&torrent_handle::is_piece_filtered))
        .def("has_metadata", _(&torrent_handle::has_metadata))
        .def("save_path", _(&torrent_handle::save_path))
        .def("move_storage", _(&torrent_handle::move_storage))
        .def("info_hash", _(&torrent_handle::info_hash), copy)
        .def("file_progress", file_progress)
        .def("trackers", range(begin_trackers, end_trackers))
        .def("replace_trackers", replace_trackers)
        .def("prioritize_files", prioritize_files)
        .def("get_peer_info", get_peer_info)
        .def("get_download_queue", get_download_queue)
        ;
}

