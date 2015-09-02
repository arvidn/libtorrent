
#if !defined BOOST_PP_IS_ITERATING || !BOOST_PP_IS_ITERATING
// set-up iteration

#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/iteration/iterate.hpp>
#include <boost/preprocessor/punctuation/comma_if.hpp>
#include <boost/preprocessor/repetition/enum_binary_params.hpp>

#define BOOST_PP_ITERATION_PARAMS_1 \
	(3, (0, TORRENT_ALERT_MANAGER_MAX_ARITY, \
	"libtorrent/aux_/alert_manager_variadic_emplace.hpp"))
#include BOOST_PP_ITERATE()


#else // BOOST_PP_IS_ITERATING

// loop body

#define I BOOST_PP_ITERATION()

		template <class T
			BOOST_PP_COMMA_IF(I)
			BOOST_PP_ENUM_PARAMS(I, typename A)>
		void emplace_alert(BOOST_PP_ENUM_BINARY_PARAMS(I, A, const& a) )
		{
			mutex::scoped_lock lock(m_mutex);
#ifndef TORRENT_NO_DEPRECATE
			if (m_dispatch)
			{
				m_dispatch(std::auto_ptr<alert>(new T(m_allocations[m_generation]
					BOOST_PP_COMMA_IF(I)
					BOOST_PP_ENUM_PARAMS(I, a))));
				return;
			}
#endif
			// don't add more than this number of alerts, unless it's a
			// high priority alert, in which case we try harder to deliver it
			// for high priority alerts, double the upper limit
			if (m_alerts[m_generation].size() >= m_queue_size_limit
				* (1 + T::priority))
				return;

			T alert(m_allocations[m_generation]
				BOOST_PP_COMMA_IF(I)
				BOOST_PP_ENUM_PARAMS(I, a));
			m_alerts[m_generation].push_back(alert);

			maybe_notify(&alert, lock);
		}

#undef I

#endif

