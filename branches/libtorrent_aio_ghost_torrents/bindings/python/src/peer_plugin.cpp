// Copyright Daniel Wallin 2007. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <cctype>
#include <iostream>

#include <libtorrent/extensions.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/lazy_entry.hpp>
#include <libtorrent/peer_request.hpp>
#include <libtorrent/disk_buffer_holder.hpp>
#include <libtorrent/bitfield.hpp>
#include <boost/python.hpp>

using namespace boost::python;
using namespace libtorrent;

namespace 
{
  struct peer_plugin_wrap : peer_plugin, wrapper<peer_plugin>
  {
      void add_handshake(entry& e)
      {
          if (override f = this->get_override("add_handshake"))
              e = call<entry>(f.ptr(), e);
          else
              peer_plugin::add_handshake(e);
      }

      void default_add_handshake(entry& e)
      {
          this->peer_plugin::add_handshake(e);
      }

      bool on_handshake(char const* reserved_bits)
      {
          if (override f = this->get_override("on_handshake"))
              return f();
          else
              return peer_plugin::on_handshake(reserved_bits);
      }

      bool default_on_handshake(char const* reserved_bits)
      {
          return this->peer_plugin::on_handshake(reserved_bits);
      }

      bool on_extension_handshake(lazy_entry const& e)
      {
          if (override f = this->get_override("on_extension_handshake"))
              return f(e);
          else
              return peer_plugin::on_extension_handshake(e);
      }

      bool default_on_extension_handshake(lazy_entry const& e)
      {
          return this->peer_plugin::on_extension_handshake(e);
      }

      bool on_choke()
      {
          if (override f = this->get_override("on_choke"))
              return f();
          else
              return peer_plugin::on_choke();
      }

      bool default_on_choke()
      {
          return this->peer_plugin::on_choke();
      }

      bool on_unchoke()
      {
          if (override f = this->get_override("on_unchoke"))
              return f();
          else
              return peer_plugin::on_unchoke();
      }

      bool default_on_unchoke()
      {
          return this->peer_plugin::on_unchoke();
      }

      bool on_interested()
      {
          if (override f = this->get_override("on_interested"))
              return f();
          else
              return peer_plugin::on_interested();
      }

      bool default_on_interested()
      {
          return this->peer_plugin::on_interested();
      }

      bool on_not_interested()
      {
          if (override f = this->get_override("on_not_interested"))
              return f();
          else
              return peer_plugin::on_not_interested();
      }

      bool default_on_not_interested()
      {
          return this->peer_plugin::on_not_interested();
      }

      bool on_have(int index)
      {
          if (override f = this->get_override("on_have"))
              return f(index);
          else
              return peer_plugin::on_have(index);
      }

      bool default_on_have(int index)
      {
          return this->peer_plugin::on_have(index);
      }

      bool on_bitfield(list _bf)
      {
          //Convert list to a bitfield
          bitfield bf(len(_bf));
          for (int i = 0; i < len(_bf); ++i)
          {
              if (_bf[i])
                  bf.set_bit(i);
              else
                  bf.clear_bit(i);
          }   
          if (override f = this->get_override("on_bitfield"))
              return f(bf);
          else
              return peer_plugin::on_bitfield(bf);
      }

      bool default_on_bitfield(const bitfield &bf)
      {
          return this->peer_plugin::on_bitfield(bf);
      }

      bool on_request(peer_request const& req)
      {
          if (override f = this->get_override("on_request"))
              return f(req);
          else
              return peer_plugin::on_request(req);
      }

      bool default_on_request(peer_request const& req)
      {
          return this->peer_plugin::on_request(req);
      }

      bool on_piece(peer_request const& piece, disk_buffer_holder& data)
      {
          if (override f = this->get_override("on_piece"))
              return f(piece, data);
          else
              return peer_plugin::on_piece(piece, data);
      }

      bool default_on_piece(peer_request const& piece, disk_buffer_holder& data)
      {
          return this->peer_plugin::on_piece(piece, data);
      }

      bool on_cancel(peer_request const& req)
      {
          if (override f = this->get_override("on_cancel"))
              return f(req);
          else
              return peer_plugin::on_cancel(req);
      }

      bool default_on_cancel(peer_request const& req)
      {
          return this->peer_plugin::on_cancel(req);
      }

      bool on_extended(int length, int msg, buffer::const_interval body)
      {
          if (override f = this->get_override("on_extended"))
              return f(length, msg, body);
          else
              return peer_plugin::on_extended(length, msg, body);
      }

      bool default_on_extended(int length, int msg, buffer::const_interval body)
      {
          return this->peer_plugin::on_extended(length, msg, body);
      }

      bool on_unknown_message(int length, int msg, buffer::const_interval body)
      {
          if (override f = this->get_override("on_unknown_message"))
              return f(length, msg, body);
          else
              return peer_plugin::on_unknown_message(length, msg, body);
      }

      bool default_on_unknown_message(int length, int msg, buffer::const_interval body)
      {
          return this->peer_plugin::on_unknown_message(length, msg, body);
      }

      void on_piece_pass(int index)
      {
          if (override f = this->get_override("on_piece_pass"))
              f(index);
          else
              peer_plugin::on_piece_pass(index);
      }

      void default_on_piece_pass(int index)
      {
          this->peer_plugin::on_piece_pass(index);
      }

      void on_piece_failed(int index)
      {
          if (override f = this->get_override("on_piece_failed"))
              f(index);
          else
              peer_plugin::on_piece_failed(index);
      }

      void default_on_piece_failed(int index)
      {
          this->peer_plugin::on_piece_failed(index);
      }

      void tick()
      {
          if (override f = this->get_override("tick"))
              f();
          else
              peer_plugin::tick();
      }

      void default_tick()
      {
          this->peer_plugin::tick();
      }

      bool write_request(peer_request const& req)
      {
          if (override f = this->get_override("write_request"))
              return f(req);
          else
              return peer_plugin::write_request(req);
      }

      bool default_write_request(peer_request const& req)
      {
          return this->peer_plugin::write_request(req);
      }
  };

  object get_buffer()
  {
      static char const data[] = "foobar";
      return object(handle<>(PyBuffer_FromMemory((void*)data, 6)));
  }

} // namespace unnamed

void bind_peer_plugin()
{
    class_<
        peer_plugin_wrap, boost::shared_ptr<peer_plugin_wrap>, boost::noncopyable
    >("peer_plugin")
        .def(
            "add_handshake"
          , &peer_plugin::add_handshake, &peer_plugin_wrap::default_add_handshake
        )
        .def(
            "on_handshake"
          , &peer_plugin::on_handshake, &peer_plugin_wrap::default_on_handshake
        )
        .def(
            "on_extension_handshake"
          , &peer_plugin::on_extension_handshake
          , &peer_plugin_wrap::default_on_extension_handshake
        )
        .def(
            "on_choke"
          , &peer_plugin::on_choke, &peer_plugin_wrap::default_on_choke
        )
        .def(
            "on_unchoke"
          , &peer_plugin::on_unchoke, &peer_plugin_wrap::default_on_unchoke
        )
        .def(
            "on_interested"
          , &peer_plugin::on_interested, &peer_plugin_wrap::default_on_interested
        )
        .def(
            "on_not_interested"
          , &peer_plugin::on_not_interested, &peer_plugin_wrap::default_on_not_interested
        )
        .def(
            "on_have"
          , &peer_plugin::on_have, &peer_plugin_wrap::default_on_have
        )
        .def(
            "on_bitfield"
          , &peer_plugin::on_bitfield, &peer_plugin_wrap::default_on_bitfield
        )
        .def(
            "on_request"
          , &peer_plugin::on_request, &peer_plugin_wrap::default_on_request
        )
        .def(
            "on_piece"
          , &peer_plugin::on_piece, &peer_plugin_wrap::default_on_piece
        )
        .def(
            "on_cancel"
          , &peer_plugin::on_cancel, &peer_plugin_wrap::default_on_cancel
        )
        .def(
            "on_piece_pass"
          , &peer_plugin::on_piece_pass, &peer_plugin_wrap::default_on_piece_pass
        )
        .def(
            "on_piece_failed"
          , &peer_plugin::on_piece_failed, &peer_plugin_wrap::default_on_piece_failed
        )
        .def(
            "tick"
          , &peer_plugin::tick, &peer_plugin_wrap::default_tick
        )
        .def(
            "write_request"
          , &peer_plugin::write_request, &peer_plugin_wrap::default_write_request
        )
        // These seem to make VC7.1 freeze. Needs special handling.
        
        /*.def(
            "on_extended"
          , &peer_plugin::on_extended, &peer_plugin_wrap::default_on_extended
        )
        .def(
            "on_unknown_message"
          , &peer_plugin::on_unknown_message, &peer_plugin_wrap::default_on_unknown_message
        )*/
        ;

    def("get_buffer", &get_buffer);
}

