// Copyright Daniel Wallin 2007. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/extensions.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/peer_request.hpp>
#include <boost/python.hpp>
#include "gil.hpp"

using namespace boost::python;
using namespace libtorrent;

namespace 
{

  struct torrent_plugin_wrap : torrent_plugin, wrapper<torrent_plugin>
  {
      boost::shared_ptr<peer_plugin> new_connection(peer_connection* p)
      {
          lock_gil lock;

          if (override f = this->get_override("new_connection"))
              return f(p);
          return torrent_plugin::new_connection(p);
      }

      boost::shared_ptr<peer_plugin> default_new_connection(peer_connection* p)
      {
          return this->torrent_plugin::new_connection(p);
      }

      void on_piece_pass(int index)
      {
          lock_gil lock;

          if (override f = this->get_override("on_piece_pass"))
              f(index);
          else
            torrent_plugin::on_piece_pass(index);
      }

      void default_on_piece_pass(int index)
      {
          this->torrent_plugin::on_piece_pass(index);
      }

      void on_piece_failed(int index)
      {
          lock_gil lock;

          if (override f = this->get_override("on_piece_failed"))
              f(index);
          else
              torrent_plugin::on_piece_failed(index);
      }

      void default_on_piece_failed(int index)
      {
          return this->torrent_plugin::on_piece_failed(index);
      }

      void tick()
      {
          lock_gil lock;

          if (override f = this->get_override("tick"))
              f();
          else
              torrent_plugin::tick();
      }

      void default_tick()
      {
          return this->torrent_plugin::tick();
      }

      bool on_pause()
      {
          lock_gil lock;

          if (override f = this->get_override("on_pause"))
              return f();
          return torrent_plugin::on_pause();
      }

      bool default_on_pause()
      {
          return this->torrent_plugin::on_pause();
      }

      bool on_resume()
      {
          lock_gil lock;

          if (override f = this->get_override("on_resume"))
              return f();
          return torrent_plugin::on_resume();
      }

      bool default_on_resume()
      {
          return this->torrent_plugin::on_resume();
      }
  };

} // namespace unnamed

void bind_extensions()
{
    class_<
        torrent_plugin_wrap, boost::shared_ptr<torrent_plugin_wrap>, boost::noncopyable
    >("torrent_plugin")
        .def(
            "new_connection"
          , &torrent_plugin::new_connection, &torrent_plugin_wrap::default_new_connection
        )
        .def(
            "on_piece_pass"
          , &torrent_plugin::on_piece_pass, &torrent_plugin_wrap::default_on_piece_pass
        )
        .def(
            "on_piece_failed"
          , &torrent_plugin::on_piece_failed, &torrent_plugin_wrap::default_on_piece_failed
        )
        .def(
            "tick"
          , &torrent_plugin::tick, &torrent_plugin_wrap::default_tick
        )
        .def(
            "on_pause"
          , &torrent_plugin::on_pause, &torrent_plugin_wrap::default_on_pause
        )
        .def(
            "on_resume"
          , &torrent_plugin::on_resume, &torrent_plugin_wrap::default_on_resume
        );
}

