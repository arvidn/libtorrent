// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/python.hpp>
#include <boost/python/tuple.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/peer_info.hpp>
#include <boost/lexical_cast.hpp>
#include "gil.hpp"

using namespace boost::python;
using namespace libtorrent;

namespace
{

  list url_seeds(torrent_handle& handle)
  {
      list ret;
      std::set<std::string> urls;
      {
          allow_threading_guard guard;
          urls = handle.url_seeds();
      }

      for (std::set<std::string>::iterator i(urls.begin())
          , end(urls.end()); i != end; ++i)
          ret.append(*i);
      return ret;
  }

  list piece_availability(torrent_handle& handle)
  {
      list ret;
      std::vector<int> avail;
      {
          allow_threading_guard guard;
          handle.piece_availability(avail);
      }

      for (std::vector<int>::iterator i(avail.begin())
          , end(avail.end()); i != end; ++i)
          ret.append(*i);
      return ret;
  }

  list piece_priorities(torrent_handle& handle)
  {
      list ret;
      std::vector<int> prio;
      {
          allow_threading_guard guard;
          prio = handle.piece_priorities();
      }

      for (std::vector<int>::iterator i(prio.begin())
          , end(prio.end()); i != end; ++i)
          ret.append(*i);
      return ret;
  }

} // namespace unnamed

list file_progress(torrent_handle& handle)
{
    std::vector<size_type> p;

    {
        allow_threading_guard guard;
        p.reserve(handle.get_torrent_info().num_files());
        handle.file_progress(p);
    }

    list result;

    for (std::vector<size_type>::iterator i(p.begin()), e(p.end()); i != e; ++i)
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
        result.append(*i);

    return result;
}

void prioritize_pieces(torrent_handle& info, object o)
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
      info.prioritize_pieces(result);
      return;
   }
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

list file_priorities(torrent_handle& handle)
{
    list ret;
    std::vector<int> priorities = handle.file_priorities();

    for (std::vector<int>::iterator i = priorities.begin(); i != priorities.end(); ++i)
        ret.append(*i);

    return ret;
}

void replace_trackers(torrent_handle& h, object trackers)
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
    h.replace_trackers(result);
}

list trackers(torrent_handle &h)
{
    list ret;
    std::vector<announce_entry> const trackers = h.trackers();
    for (std::vector<announce_entry>::const_iterator i = trackers.begin(), end(trackers.end()); i != end; ++i)
    {
        dict d;
        d["url"] = i->url;
        d["tier"] = i->tier;
        d["fail_limit"] = i->fail_limit;
        d["fails"] = i->fails;
        d["source"] = i->source;
        d["verified"] = i->verified;
        d["updating"] = i->updating;
        d["start_sent"] = i->start_sent;
        d["complete_sent"] = i->complete_sent;
        ret.append(d);
    }
    return ret;
}

list get_download_queue(torrent_handle& handle)
{
    using boost::python::make_tuple;

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
            block_info["peer"] = make_tuple(
                boost::lexical_cast<std::string>(i->blocks[k].peer().address()), i->blocks[k].peer().port());
            block_list.append(block_info);
        }
        partial_piece["blocks"] = block_list;

        ret.append(partial_piece);
    }

    return ret;
}

namespace
{
    tcp::endpoint tuple_to_endpoint(tuple const& t)
    {
        return tcp::endpoint(address::from_string(extract<std::string>(t[0])), extract<int>(t[1]));
    }
}

void force_reannounce(torrent_handle& th, int s)
{
    th.force_reannounce(boost::posix_time::seconds(s));
}

void connect_peer(torrent_handle& th, tuple ip, int source)
{
    th.connect_peer(tuple_to_endpoint(ip), source);
}

void set_peer_upload_limit(torrent_handle& th, tuple const& ip, int limit)
{
    th.set_peer_upload_limit(tuple_to_endpoint(ip), limit);
}

void set_peer_download_limit(torrent_handle& th, tuple const& ip, int limit)
{
    th.set_peer_download_limit(tuple_to_endpoint(ip), limit);
}

void add_piece(torrent_handle& th, int piece, char const *data, int flags)
{
   th.add_piece(piece, data, flags);
}

void bind_torrent_handle()
{
    void (torrent_handle::*force_reannounce0)() const = &torrent_handle::force_reannounce;

    int (torrent_handle::*piece_priority0)(int) const = &torrent_handle::piece_priority;
    void (torrent_handle::*piece_priority1)(int, int) const = &torrent_handle::piece_priority;

    void (torrent_handle::*move_storage0)(fs::path const&) const = &torrent_handle::move_storage;
    void (torrent_handle::*move_storage1)(fs::wpath const&) const = &torrent_handle::move_storage;

    void (torrent_handle::*rename_file0)(int, fs::path const&) const = &torrent_handle::rename_file;
    void (torrent_handle::*rename_file1)(int, fs::wpath const&) const = &torrent_handle::rename_file;

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
    bool (torrent_handle::*resolve_countries0)() const = &torrent_handle::resolve_countries;
    void (torrent_handle::*resolve_countries1)(bool) = &torrent_handle::resolve_countries;
#endif

#define _ allow_threads

    class_<torrent_handle>("torrent_handle")
        .def("get_peer_info", get_peer_info)
        .def("status", _(&torrent_handle::status))
        .def("get_download_queue", get_download_queue)
        .def("file_progress", file_progress)
        .def("trackers", trackers)
        .def("replace_trackers", replace_trackers)
        .def("add_url_seed", _(&torrent_handle::add_url_seed))
        .def("remove_url_seed", _(&torrent_handle::remove_url_seed))
        .def("url_seeds", url_seeds)
        .def("has_metadata", _(&torrent_handle::has_metadata))
        .def("get_torrent_info", _(&torrent_handle::get_torrent_info), return_internal_reference<>())
        .def("is_valid", _(&torrent_handle::is_valid))
        .def("is_seed", _(&torrent_handle::is_seed))
        .def("is_finished", _(&torrent_handle::is_finished))
        .def("is_paused", _(&torrent_handle::is_paused))
        .def("pause", _(&torrent_handle::pause))
        .def("resume", _(&torrent_handle::resume))
        .def("clear_error", _(&torrent_handle::clear_error))
        .def("set_priority", _(&torrent_handle::set_priority))

        .def("is_auto_managed", _(&torrent_handle::is_auto_managed))
        .def("auto_managed", _(&torrent_handle::auto_managed))
        .def("queue_position", _(&torrent_handle::queue_position))
        .def("queue_position_up", _(&torrent_handle::queue_position_up))
        .def("queue_position_down", _(&torrent_handle::queue_position_down))
        .def("queue_position_top", _(&torrent_handle::queue_position_top))
        .def("queue_position_bottom", _(&torrent_handle::queue_position_bottom))

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
        .def("resolve_countries", _(resolve_countries0))
        .def("resolve_countries", _(resolve_countries1))
#endif
        // deprecated
#ifndef TORRENT_NO_DEPRECATE
        .def("filter_piece", _(&torrent_handle::filter_piece))
        .def("is_piece_filtered", _(&torrent_handle::is_piece_filtered))
        .def("write_resume_data", _(&torrent_handle::write_resume_data))
        .def("is_seed", _(&torrent_handle::is_seed))
        .def("is_finished", _(&torrent_handle::is_finished))
        .def("is_paused", _(&torrent_handle::is_paused))
        .def("is_auto_managed", _(&torrent_handle::is_auto_managed))
        .def("has_metadata", _(&torrent_handle::has_metadata))
#endif
        .def("add_piece", add_piece)
        .def("read_piece", _(&torrent_handle::read_piece))
        .def("set_piece_deadline", _(&torrent_handle::set_piece_deadline)
            , (arg("index"), arg("deadline"), arg("flags") = 0))
        .def("piece_availability", &piece_availability)
        .def("piece_priority", _(piece_priority0))
        .def("piece_priority", _(piece_priority1))
        .def("prioritize_pieces", &prioritize_pieces)
        .def("piece_priorities", &piece_priorities)
        .def("prioritize_files", &prioritize_files)
        .def("file_priorities", &file_priorities)
        .def("use_interface", &torrent_handle::use_interface)
        .def("save_resume_data", _(&torrent_handle::save_resume_data))
        .def("force_reannounce", _(force_reannounce0))
        .def("force_reannounce", &force_reannounce)
        .def("force_dht_announce", _(&torrent_handle::force_dht_announce))
        .def("scrape_tracker", _(&torrent_handle::scrape_tracker))
        .def("name", _(&torrent_handle::name))
        .def("set_upload_limit", _(&torrent_handle::set_upload_limit))
        .def("upload_limit", _(&torrent_handle::upload_limit))
        .def("set_download_limit", _(&torrent_handle::set_download_limit))
        .def("download_limit", _(&torrent_handle::download_limit))
        .def("set_sequential_download", _(&torrent_handle::set_sequential_download))
        .def("set_peer_upload_limit", &set_peer_upload_limit)
        .def("set_peer_download_limit", &set_peer_download_limit)
        .def("connect_peer", &connect_peer)
        .def("set_ratio", _(&torrent_handle::set_ratio))
        .def("save_path", _(&torrent_handle::save_path))
        .def("set_max_uploads", _(&torrent_handle::set_max_uploads))
        .def("set_max_connections", _(&torrent_handle::set_max_connections))
        .def("set_tracker_login", _(&torrent_handle::set_tracker_login))
        .def("move_storage", _(move_storage0))
        .def("move_storage", _(move_storage1))
        .def("info_hash", _(&torrent_handle::info_hash))
        .def("force_recheck", _(&torrent_handle::force_recheck))
        .def("rename_file", _(rename_file0))
        .def("rename_file", _(rename_file1))
        ;

    enum_<torrent_handle::deadline_flags>("deadline_flags")
        .value("alert_when_available", torrent_handle::alert_when_available)
    ;

}
