
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
		bool emplace_alert(BOOST_PP_ENUM_BINARY_PARAMS(I, A, const& a) )
		{
			// acquire a shared lock
			shared_lock::scoped_lock lock(m_shared_lock, shared_lock::shared);

			// allocate thread specific storage the first time the thread runs
			// this will be freed automagically by boost::thread_specific_ptr
			if (m_thread_storage.get() == NULL)
				init_thread_storage();

			// get the thread specific storage
			thread_storage*const ts = m_thread_storage.get();

#ifndef TORRENT_NO_DEPRECATE
			if (m_dispatch)
			{
				m_dispatch(std::auto_ptr<alert>(new T(ts->current_allocator()
					BOOST_PP_COMMA_IF(I)
					BOOST_PP_ENUM_PARAMS(I, a))));
				return false;
			}
#endif
			int index;
			T* alert;
			alert = m_alerts_pool.acquire(alert);
			new (alert) T(ts->current_allocator()
					BOOST_PP_COMMA_IF(I)
					BOOST_PP_ENUM_PARAMS(I, a));
			TORRENT_ASSERT(alert != NULL);

			if ((index = m_alerts.push(alert, T::priority)) == -1)
			{
#ifndef TORRENT_DISABLE_EXTENSIONS
				if (!m_ses_extensions_reliable.empty())
					notify_extensions(alert, m_ses_extensions_reliable);
#endif
				// free the alert
				m_alerts_pool.release(alert);
				return false;
			}

			if (index == 0)
			{
				// we just posted to an empty queue. If anyone is waiting for
				// alerts, we need to notify them. Also (potentially) call the
				// user supplied m_notify callback to let the client wake up its
				// message loop to poll for alerts.
				if (m_notify) m_notify();

				// wake any threads waiting for alerts
				mutex::scoped_lock lock(m_mutex);
				m_condition.notify_all();
			}

#ifndef TORRENT_DISABLE_EXTENSIONS
			notify_extensions(alert, m_ses_extensions);
#endif

			return true;
		}

#undef I

#endif

