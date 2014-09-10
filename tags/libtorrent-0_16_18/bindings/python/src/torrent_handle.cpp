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

  list http_seeds(torrent_handle& handle)
  {
      list ret;
      std::set<std::string> urls;
      {
          allow_threading_guard guard;
          urls = handle.http_seeds();
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

list file_progress(torrent_handle& handle, int flags)
{
    std::vector<size_type> p;

    {
        allow_threading_guard guard;
        p.reserve(handle.get_torrent_info().num_files());
        handle.file_progress(p, flags);
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

int file_prioritity0(torrent_handle& h, int index)
{
   return h.file_priority(index);
}

void file_prioritity1(torrent_handle& h, int index, int prio)
{
   return h.file_priority(index, prio);
}

void dict_to_announce_entry(dict d, announce_entry& ae)
{
   ae.url = extract<std::string>(d["url"]);
   if (d.has_key("tier"))
      ae.tier = extract<int>(d["tier"]);
   if (d.has_key("fail_limit"))
      ae.fail_limit = extract<int>(d["fail_limit"]);
   if (d.has_key("source"))
      ae.source = extract<int>(d["source"]);
   if (d.has_key("verified"))
      ae.verified = extract<int>(d["verified"]);
   if (d.has_key("send_stats"))
      ae.send_stats = extract<int>(d["send_stats"]);
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

        if (extract<announce_entry>(object(entry)).check())
        {
           result.push_back(extract<announce_entry>(object(entry)));
        }
        else
        {
            dict d;
            d = extract<dict>(object(entry));
            announce_entry ae;
            dict_to_announce_entry(d, ae);
            result.push_back(ae);
        }
    }

    allow_threading_guard guard;
    h.replace_trackers(result);
}

void add_tracker(torrent_handle& h, dict d)
{
   announce_entry ae;
   dict_to_announce_entry(d, ae);
   h.add_tracker(ae);
}

list trackers(torrent_handle& h)
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
        d["send_stats"] = i->send_stats;
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

void set_metadata(torrent_handle& handle, std::string const& buf)
{
   handle.set_metadata(buf.c_str(), buf.size());
}

namespace
{
    tcp::endpoint tuple_to_endpoint(tuple const& t)
    {
        return tcp::endpoint(address::from_string(extract<std::string>(t[0])), extract<int>(t[1]));
    }
}

#if BOOST_VERSION > 104200

boost::intrusive_ptr<const torrent_info> get_torrent_info(torrent_handle const& h)
{
	return boost::intrusive_ptr<const torrent_info>(&h.get_torrent_info());
}

#else

boost::intrusive_ptr<torrent_info> get_torrent_info(torrent_handle const& h)
{
	// I can't figure out how to expose intrusive_ptr<const torrent_info>
	// as well as supporting mutable instances. So, this hack is better
	// than compilation errors. It seems to work on newer versions of boost though
   return boost::intrusive_ptr<torrent_info>(const_cast<torrent_info*>(&h.get_torrent_info()));
}

#endif

void force_reannounce(torrent_handle& th, int s)
{
    th.force_reannounce(boost::posix_time::seconds(s));
}

void connect_peer(torrent_handle& th, tuple ip, int source)
{
    th.connect_peer(tuple_to_endpoint(ip), source);
}

#ifndef TORRENT_NO_DEPRECATE
void set_peer_upload_limit(torrent_handle& th, tuple const& ip, int limit)
{
    th.set_peer_upload_limit(tuple_to_endpoint(ip), limit);
}

void set_peer_download_limit(torrent_handle& th, tuple const& ip, int limit)
{
    th.set_peer_download_limit(tuple_to_endpoint(ip), limit);
}
#endif

void add_piece(torrent_handle& th, int piece, char const *data, int flags)
{
   th.add_piece(piece, data, flags);
}

void bind_torrent_handle()
{
    void (torrent_handle::*force_reannounce0)() const = &torrent_handle::force_reannounce;

#ifndef TORRENT_NO_DEPRECATE
    bool (torrent_handle::*super_seeding0)() const = &torrent_handle::super_seeding;
#endif
    void (torrent_handle::*super_seeding1)(bool) const = &torrent_handle::super_seeding;

    int (torrent_handle::*piece_priority0)(int) const = &torrent_handle::piece_priority;
    void (torrent_handle::*piece_priority1)(int, int) const = &torrent_handle::piece_priority;

    void (torrent_handle::*move_storage0)(std::string const&) const = &torrent_handle::move_storage;
    void (torrent_handle::*rename_file0)(int, std::string const&) const = &torrent_handle::rename_file;

#if TORRENT_USE_WSTRING && !defined TORRENT_NO_DEPRECATE
    void (torrent_handle::*move_storage1)(std::wstring const&) const = &torrent_handle::move_storage;
    void (torrent_handle::*rename_file1)(int, std::wstring const&) const = &torrent_handle::rename_file;
#endif

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
    bool (torrent_handle::*resolve_countries0)() const = &torrent_handle::resolve_countries;
    void (torrent_handle::*resolve_countries1)(bool) = &torrent_handle::resolve_countries;
#endif

#define _ allow_threads

    class_<torrent_handle>("torrent_handle")
        .def(self == self)
        .def(self != self)
        .def(self < self)
        .def("get_peer_info", get_peer_info)
        .def("status", _(&torrent_handle::status), arg("flags") = 0xffffffff)
        .def("get_download_queue", get_download_queue)
        .def("file_progress", file_progress, arg("flags") = 0)
        .def("trackers", trackers)
        .def("replace_trackers", replace_trackers)
        .def("add_tracker", add_tracker)
        .def("add_url_seed", _(&torrent_handle::add_url_seed))
        .def("remove_url_seed", _(&torrent_handle::remove_url_seed))
        .def("url_seeds", url_seeds)
        .def("add_http_seed", _(&torrent_handle::add_http_seed))
        .def("remove_http_seed", _(&torrent_handle::remove_http_seed))
        .def("http_seeds", http_seeds)
        .def("get_torrent_info", get_torrent_info)
        .def("set_metadata", set_metadata)
        .def("is_valid", _(&torrent_handle::is_valid))
        .def("pause", _(&torrent_handle::pause), arg("flags") = 0)
        .def("resume", _(&torrent_handle::resume))
        .def("clear_error", _(&torrent_handle::clear_error))
        .def("set_priority", _(&torrent_handle::set_priority))
        .def("super_seeding", super_seeding1)

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
        .def("super_seeding", super_seeding0)
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
        .def("have_piece", _(&torrent_handle::have_piece))
        .def("set_piece_deadline", _(&torrent_handle::set_piece_deadline)
            , (arg("index"), arg("deadline"), arg("flags") = 0))
        .def("reset_piece_deadline", _(&torrent_handle::reset_piece_deadline), (arg("index")))
        .def("piece_availability", &piece_availability)
        .def("piece_priority", _(piece_priority0))
        .def("piece_priority", _(piece_priority1))
        .def("prioritize_pieces", &prioritize_pieces)
        .def("piece_priorities", &piece_priorities)
        .def("prioritize_files", &prioritize_files)
        .def("file_priorities", &file_priorities)
        .def("file_priority", &file_prioritity0)
        .def("file_priority", &file_prioritity1)
        .def("use_interface", &torrent_handle::use_interface)
        .def("save_resume_data", _(&torrent_handle::save_resume_data), arg("flags") = 0)
        .def("need_save_resume_data", _(&torrent_handle::need_save_resume_data))
        .def("force_reannounce", _(force_reannounce0))
        .def("force_reannounce", &force_reannounce)
#ifndef TORRENT_DISABLE_DHT
        .def("force_dht_announce", _(&torrent_handle::force_dht_announce))
#endif
        .def("scrape_tracker", _(&torrent_handle::scrape_tracker))
        .def("name", _(&torrent_handle::name))
        .def("set_upload_mode", _(&torrent_handle::set_upload_mode))
        .def("set_share_mode", _(&torrent_handle::set_share_mode))
        .def("flush_cache", &torrent_handle::flush_cache)
        .def("apply_ip_filter", &torrent_handle::apply_ip_filter)
        .def("set_upload_limit", _(&torrent_handle::set_upload_limit))
        .def("upload_limit", _(&torrent_handle::upload_limit))
        .def("set_download_limit", _(&torrent_handle::set_download_limit))
        .def("download_limit", _(&torrent_handle::download_limit))
        .def("set_sequential_download", _(&torrent_handle::set_sequential_download))
#ifndef TORRENT_NO_DEPRECATE
        .def("set_peer_upload_limit", &set_peer_upload_limit)
        .def("set_peer_download_limit", &set_peer_download_limit)
        .def("set_ratio", _(&torrent_handle::set_ratio))
#endif
        .def("connect_peer", &connect_peer)
        .def("save_path", _(&torrent_handle::save_path))
        .def("set_max_uploads", _(&torrent_handle::set_max_uploads))
        .def("max_uploads", _(&torrent_handle::max_uploads))
        .def("set_max_connections", _(&torrent_handle::set_max_connections))
        .def("max_connections", _(&torrent_handle::max_connections))
        .def("set_tracker_login", _(&torrent_handle::set_tracker_login))
        .def("move_storage", _(move_storage0))
        .def("info_hash", _(&torrent_handle::info_hash))
        .def("force_recheck", _(&torrent_handle::force_recheck))
        .def("rename_file", _(rename_file0))
        .def("set_ssl_certificate", &torrent_handle::set_ssl_certificate, (arg("cert"), arg("private_key"), arg("dh_params"), arg("passphrase")=""))
#if TORRENT_USE_WSTRING && !defined TORRENT_NO_DEPRECATE
        .def("move_storage", _(move_storage1))
        .def("rename_file", _(rename_file1))
#endif
        ;

    enum_<torrent_handle::file_progress_flags_t>("file_progress_flags")
        .value("piece_granularity", torrent_handle::piece_granularity)
    ;

    enum_<torrent_handle::pause_flags_t>("pause_flags_t")
        .value("graceful_pause", torrent_handle::graceful_pause)
    ;

    enum_<torrent_handle::save_resume_flags_t>("save_resume_flags_t")
        .value("flush_disk_cache", torrent_handle::flush_disk_cache)
    ;

    enum_<torrent_handle::deadline_flags>("deadline_flags")
        .value("alert_when_available", torrent_handle::alert_when_available)
    ;

    enum_<torrent_handle::status_flags_t>("status_flags_t")
        .value("query_distributed_copies", torrent_handle::query_distributed_copies)
        .value("query_accurate_download_counters", torrent_handle::query_accurate_download_counters)
        .value("query_last_seen_complete", torrent_handle::query_last_seen_complete)
        .value("query_pieces", torrent_handle::query_pieces)
        .value("query_verified_pieces", torrent_handle::query_verified_pieces)
    ;
}
