// Copyright Daniel Wallin and Arvid Norberg 2007.
// Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef VARIANT_STREAM_070211_HPP
# define VARIANT_STREAM_070211_HPP

# include <boost/variant.hpp>

# include <boost/mpl/vector.hpp>
# include <boost/mpl/void.hpp>
# include <boost/mpl/remove.hpp>
# include <boost/mpl/transform.hpp>
# include <boost/mpl/size.hpp>

# include <boost/preprocessor/repetition/enum_params.hpp>
# include <boost/preprocessor/repetition/enum_binary_params.hpp>
# include <boost/preprocessor/facilities/intercept.hpp>

# include <boost/type_traits/add_pointer.hpp>
# include <boost/noncopyable.hpp>

#include <asio/io_service.hpp>

# define NETWORK_VARIANT_STREAM_LIMIT 5

namespace libtorrent {

namespace aux
{

  struct delete_visitor
    : boost::static_visitor<>
  {
      template <class T>
      void operator()(T* p) const
      {
          delete p;
      }

      void operator()(boost::blank) const
      {}
  };

// -------------- async_connect -----------

  template <class EndpointType, class Handler>
  struct async_connect_visitor
    : boost::static_visitor<>
  {
      async_connect_visitor(EndpointType const& endpoint, Handler const& handler)
        : endpoint(endpoint)
        , handler(handler)
      {}

      template <class T>
      void operator()(T* p) const
      {
          p->async_connect(endpoint, handler);
      }

      void operator()(boost::blank) const
      {}

      EndpointType const& endpoint;
      Handler const& handler;
  };

// -------------- bind -----------

  template <class EndpointType, class Error_Handler = boost::mpl::void_>
  struct bind_visitor
    : boost::static_visitor<>
  {
      bind_visitor(EndpointType const& ep, Error_Handler const& error_handler)
        : endpoint(ep)
        , error_handler(error_handler)
      {}

      template <class T>
      void operator()(T* p) const
      {
          p->bind(endpoint, error_handler);
      }

      void operator()(boost::blank) const
      {}

      EndpointType const& endpoint;
      Error_Handler const& error_handler;
  };

  template <class EndpointType>
  struct bind_visitor<EndpointType, boost::mpl::void_>
    : boost::static_visitor<>
  {
      bind_visitor(EndpointType const& ep)
        : endpoint(ep)
      {}

      template <class T>
      void operator()(T* p) const
      {
          p->bind(endpoint);
      }

      void operator()(boost::blank) const
      {}

      EndpointType const& endpoint;
  };

// -------------- open -----------

  template <class Protocol, class Error_Handler = boost::mpl::void_>
  struct open_visitor
    : boost::static_visitor<>
  {
      open_visitor(Protocol const& p, Error_Handler const& error_handler)
        : proto(p)
        , error_handler(error_handler)
      {}

      template <class T>
      void operator()(T* p) const
      {
          p->open(proto, error_handler);
      }

      void operator()(boost::blank) const
      {}

      Protocol const& proto;
      Error_Handler const& error_handler;
  };

  template <class Protocol>
  struct open_visitor<Protocol, boost::mpl::void_>
    : boost::static_visitor<>
  {
      open_visitor(Protocol const& p)
        : proto(p)
      {}

      template <class T>
      void operator()(T* p) const
      {
          p->open(proto);
      }

      void operator()(boost::blank) const
      {}

      Protocol const& proto;
  };

// -------------- close -----------

  template <class Error_Handler = boost::mpl::void_>
  struct close_visitor
    : boost::static_visitor<>
  {
      close_visitor(Error_Handler const& error_handler)
        : error_handler(error_handler)
      {}

      template <class T>
      void operator()(T* p) const
      {
          p->close(error_handler);
      }

      void operator()(boost::blank) const
      {}

      Error_Handler const& error_handler;
  };

  template <>
  struct close_visitor<boost::mpl::void_>
    : boost::static_visitor<>
  {
      template <class T>
      void operator()(T* p) const
      {
          p->close();
      }

      void operator()(boost::blank) const
      {}
  };

// -------------- remote_endpoint -----------

  template <class EndpointType, class Error_Handler = boost::mpl::void_>
  struct remote_endpoint_visitor
    : boost::static_visitor<EndpointType>
  {
      remote_endpoint_visitor(Error_Handler const& error_handler)
        : error_handler(error_handler)
      {}

      template <class T>
      EndpointType operator()(T* p) const
      {
          return p->remote_endpoint(error_handler);
      }

      EndpointType operator()(boost::blank) const
      {
          return EndpointType();
      }

      Error_Handler const& error_handler;
  };

  template <class EndpointType>
  struct remote_endpoint_visitor<EndpointType, boost::mpl::void_>
    : boost::static_visitor<EndpointType>
  {
      template <class T>
      EndpointType operator()(T* p) const
      {
          return p->remote_endpoint();
      }

      EndpointType operator()(boost::blank) const
      {
          return EndpointType();
      }
  };

// -------------- local_endpoint -----------

  template <class EndpointType, class Error_Handler = boost::mpl::void_>
  struct local_endpoint_visitor
    : boost::static_visitor<EndpointType>
  {
      local_endpoint_visitor(Error_Handler const& error_handler)
        : error_handler(error_handler)
      {}

      template <class T>
      EndpointType operator()(T* p) const
      {
          return p->local_endpoint(error_handler);
      }

      EndpointType operator()(boost::blank) const
      {
          return EndpointType();
      }

      Error_Handler const& error_handler;
  };

  template <class EndpointType>
  struct local_endpoint_visitor<EndpointType, boost::mpl::void_>
    : boost::static_visitor<EndpointType>
  {
      template <class T>
      EndpointType operator()(T* p) const
      {
          return p->local_endpoint();
      }

      EndpointType operator()(boost::blank) const
      {
          return EndpointType();
      }
  };

// -------------- async_read_some -----------

  template <class Mutable_Buffers, class Handler>
  struct async_read_some_visitor
    : boost::static_visitor<>
  {
      async_read_some_visitor(Mutable_Buffers const& buffers, Handler const& handler)
        : buffers(buffers)
        , handler(handler)
      {}

      template <class T>
      void operator()(T* p) const
      {
          p->async_read_some(buffers, handler);
      }
      void operator()(boost::blank) const
      {}

      Mutable_Buffers const& buffers;
      Handler const& handler;
  };

// -------------- async_write_some -----------

  template <class Const_Buffers, class Handler>
  struct async_write_some_visitor
    : boost::static_visitor<>
  {
      async_write_some_visitor(Const_Buffers const& buffers, Handler const& handler)
        : buffers(buffers)
        , handler(handler)
      {}

      template <class T>
      void operator()(T* p) const
      {
          p->async_write_some(buffers, handler);
      }

      void operator()(boost::blank) const
      {}

      Const_Buffers const& buffers;
      Handler const& handler;
  };

// -------------- in_avail -----------

  template <class Error_Handler = boost::mpl::void_>
  struct in_avail_visitor
    : boost::static_visitor<std::size_t>
  {
      in_avail_visitor(Error_Handler const& error_handler)
        : error_handler(error_handler)
      {}

      template <class T>
      std::size_t operator()(T* p) const
      {
          return p->in_avail(error_handler);
      }

      std::size_t operator()(boost::blank) const
      {
          return 0;
      }

      Error_Handler const& error_handler;
  };

  template <>
  struct in_avail_visitor<boost::mpl::void_>
    : boost::static_visitor<std::size_t>
  {
      template <class T>
      std::size_t operator()(T* p) const
      {
          return p->in_avail();
      }

      void operator()(boost::blank) const
      {}
  };

// -------------- io_service -----------

  template <class IOService>
  struct io_service_visitor
    : boost::static_visitor<IOService&>
  {
      template <class T>
      IOService& operator()(T* p) const
      {
          return p->io_service();
      }

      IOService& operator()(boost::blank) const
      {
          return *(IOService*)0;
      }
  };

// -------------- lowest_layer -----------

  template <class LowestLayer>
  struct lowest_layer_visitor
    : boost::static_visitor<LowestLayer&>
  {
      template <class T>
      LowestLayer& operator()(T* p) const
      {
          return p->lowest_layer();
      }

      LowestLayer& operator()(boost::blank) const
      {
          return *(LowestLayer*)0;
      }
  };

} // namespace aux

template <
    BOOST_PP_ENUM_BINARY_PARAMS(
        NETWORK_VARIANT_STREAM_LIMIT, class S, = boost::mpl::void_ BOOST_PP_INTERCEPT
    )
>
class variant_stream : boost::noncopyable
{
public:
    typedef BOOST_PP_CAT(boost::mpl::vector, NETWORK_VARIANT_STREAM_LIMIT)<
        BOOST_PP_ENUM_PARAMS(NETWORK_VARIANT_STREAM_LIMIT, S)
    > types0;

    typedef typename boost::mpl::remove<types0, boost::mpl::void_>::type types;

    typedef typename boost::make_variant_over<
        typename boost::mpl::push_back<
            typename boost::mpl::transform<
                types
              , boost::add_pointer<boost::mpl::_>
            >::type
          , boost::blank
        >::type
    >::type variant_type;

    typedef typename S0::lowest_layer_type lowest_layer_type;
    typedef typename S0::endpoint_type endpoint_type;
    typedef typename S0::protocol_type protocol_type;

    explicit variant_stream(asio::io_service& io_service)
      : m_io_service(io_service)
      , m_variant(boost::blank())
    {}

    template <class S>
    void instantiate()
    {
        std::auto_ptr<S> owned(new S(m_io_service));
        boost::apply_visitor(aux::delete_visitor(), m_variant);
        m_variant = owned.get();
        owned.release();
    }

    template <class S>
    S& get()
    {
	     return *boost::get<S*>(m_variant);
    }

    bool instantiated() const
    {
        return m_variant.which() != boost::mpl::size<types>::value;
    }

    ~variant_stream()
    {
        boost::apply_visitor(aux::delete_visitor(), m_variant);
    }

    template <class Mutable_Buffers, class Handler>
    void async_read_some(Mutable_Buffers const& buffers, Handler const& handler)
    {
        assert(instantiated());
        boost::apply_visitor(
            aux::async_read_some_visitor<Mutable_Buffers, Handler>(buffers, handler)
          , m_variant
        );
    }

    template <class Const_Buffers, class Handler>
    void async_write_some(Const_Buffers const& buffers, Handler const& handler)
    {
        assert(instantiated());
        boost::apply_visitor(
            aux::async_write_some_visitor<Const_Buffers, Handler>(buffers, handler)
          , m_variant
        );
    }

    template <class Handler>
    void async_connect(endpoint_type const& endpoint, Handler const& handler)
    {
        assert(instantiated());
        boost::apply_visitor(
            aux::async_connect_visitor<endpoint_type, Handler>(endpoint, handler), m_variant
        );
    }

    void bind(endpoint_type const& endpoint)
    {
        assert(instantiated());
        boost::apply_visitor(aux::bind_visitor<endpoint_type>(endpoint), m_variant);
    }

    template <class Error_Handler>
    void bind(endpoint_type const& endpoint, Error_Handler const& error_handler)
    {
        assert(instantiated());
        boost::apply_visitor(
            aux::bind_visitor<endpoint_type, Error_Handler>(endpoint, error_handler), m_variant
        );
    }

    void open(protocol_type const& p)
    {
        assert(instantiated());
        boost::apply_visitor(aux::open_visitor<protocol_type>(p), m_variant);
    }

    template <class Error_Handler>
    void open(protocol_type const& p, Error_Handler const& error_handler)
    {
        assert(instantiated());
        boost::apply_visitor(
            aux::open_visitor<protocol_type, Error_Handler>(p, error_handler), m_variant
        );
    }

    void close()
    {
        assert(instantiated());
        boost::apply_visitor(aux::close_visitor<>(), m_variant);
    }

    template <class Error_Handler>
    void close(Error_Handler const& error_handler)
    {
        assert(instantiated());
        boost::apply_visitor(
            aux::close_visitor<Error_Handler>(error_handler), m_variant
        );
    }

    std::size_t in_avail()
    {
        assert(instantiated());
        return boost::apply_visitor(aux::in_avail_visitor<>(), m_variant);
    }

    template <class Error_Handler>
    std::size_t in_avail(Error_Handler const& error_handler)
    {
        assert(instantiated());
        return boost::apply_visitor(
            aux::in_avail_visitor<Error_Handler>(error_handler), m_variant
        );
    }

    endpoint_type remote_endpoint()
    {
        assert(instantiated());
        return boost::apply_visitor(aux::remote_endpoint_visitor<endpoint_type>(), m_variant);
    }

    template <class Error_Handler>
    endpoint_type remote_endpoint(Error_Handler const& error_handler)
    {
        assert(instantiated());
        return boost::apply_visitor(
            aux::remote_endpoint_visitor<endpoint_type, Error_Handler>(error_handler), m_variant
        );
    }

    endpoint_type local_endpoint()
    {
        assert(instantiated());
        return boost::apply_visitor(aux::local_endpoint_visitor<endpoint_type>(), m_variant);
    }

    template <class Error_Handler>
    endpoint_type local_endpoint(Error_Handler const& error_handler)
    {
        assert(instantiated());
        return boost::apply_visitor(
            aux::local_endpoint_visitor<endpoint_type, Error_Handler>(error_handler), m_variant
        );
    }

	 asio::io_service& io_service()
    {
        assert(instantiated());
        return boost::apply_visitor(
            aux::io_service_visitor<asio::io_service>(), m_variant
        );
    }

    lowest_layer_type& lowest_layer()
    {
        assert(instantiated());
        return boost::apply_visitor(
            aux::lowest_layer_visitor<lowest_layer_type>(), m_variant
        );
    }

private:
	 asio::io_service& m_io_service;
    variant_type m_variant;
};

} // namespace libtorrent

#endif // VARIANT_STREAM_070211_HPP

