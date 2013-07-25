/*

Copyright (c) 2010-2012, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_RSS_HPP_INCLUDED
#define TORRENT_RSS_HPP_INCLUDED

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/size_type.hpp"

#include <boost/enable_shared_from_this.hpp>
#include <string>

namespace libtorrent
{
	namespace aux
	{ struct session_impl; }

	struct TORRENT_EXPORT feed_item
	{
		feed_item();
		~feed_item();
		std::string url;
		std::string uuid;
		std::string title;
		std::string description;
		std::string comment;
		std::string category;
		size_type size;
		torrent_handle handle;
		sha1_hash info_hash;
	};

#ifndef BOOST_NO_EXCEPTIONS
	torrent_handle TORRENT_EXPORT add_feed_item(session& s, feed_item const& fi
		, add_torrent_params const& p);
#endif
	torrent_handle TORRENT_EXPORT add_feed_item(session& s, feed_item const& fi
		, add_torrent_params const& p, error_code& ec);

	// the feed_settings object is all the information
	// and configuration for a specific feed. All of
	// these settings can be changed by the user
	// after adding the feed
	struct TORRENT_EXPORT feed_settings
	{
		feed_settings()
			: auto_download(true)
			, auto_map_handles(true)
			, default_ttl(30)
		{}

   	std::string url;

		// By default ``auto_download`` is true, which means all torrents in
		// the feed will be downloaded. Set this to false in order to manually
		// add torrents to the session. You may react to the rss_alert_ when
		// a feed has been updated to poll it for the new items in the feed
		// when adding torrents manually. When torrents are added automatically,
		// an add_torrent_alert_ is posted which includes the torrent handle
		// as well as the error code if it failed to be added. You may also call
		// ``session::get_torrents()`` to get the handles to the new torrents.
		bool auto_download;

		// ``auto_map_handles`` defaults to true and determines whether or
		// not to set the ``handle`` field in the ``feed_item``, returned
		// as the feed status. If auto-download is enabled, this setting
		// is ignored. If auto-download is not set, setting this to false
		// will save one pass through all the feed items trying to find
		// corresponding torrents in the session.
		bool auto_map_handles;

		// The ``default_ttl`` is the default interval for refreshing a feed.
		// This may be overridden by the feed itself (by specifying the ``<ttl>``
		// tag) and defaults to 30 minutes. The field specifies the number of
		// minutes between refreshes.
		int default_ttl;

		// If torrents are added automatically, you may want to set the
		// ``add_args`` to appropriate values for download directory etc.
		// This object is used as a template for adding torrents from feeds,
		// but some torrent specific fields will be overridden by the
		// individual torrent being added. For more information on the
		// ``add_torrent_params``, see `async_add_torrent() add_torrent()`_.
		add_torrent_params add_args;
	};

	struct TORRENT_EXPORT feed_status
	{
		feed_status(): last_update(0), next_update(0)
			, updating(false), ttl(0) {}
		std::string url;
		std::string title;
		std::string description;
		time_t last_update;
		int next_update;
		bool updating;
		std::vector<feed_item> items;
		error_code error;
		int ttl;
	};

	struct feed;

	struct TORRENT_EXPORT feed_handle
	{
		feed_handle() {}
		void update_feed();
		feed_status get_feed_status() const;
		void set_settings(feed_settings const& s);
		feed_settings settings() const;
	private:
		friend struct aux::session_impl;
		friend struct feed;
		feed_handle(boost::weak_ptr<feed> const& p);
		boost::weak_ptr<feed> m_feed_ptr;
	};

	struct feed_state;
	class http_parser;

	boost::shared_ptr<feed> TORRENT_EXPORT new_feed(aux::session_impl& ses, feed_settings const& sett);

	// this is the internal object holding all state about an
	// RSS feed. All user interaction with this object
	// goes through the feed_handle, which makes sure all calls
	// are posted to the network thread
	struct TORRENT_EXTRA_EXPORT feed : boost::enable_shared_from_this<feed>
	{
		friend void parse_feed(feed_state& f, int token, char const* name, char const* val);

		feed(aux::session_impl& ses, feed_settings const& feed);

		void on_feed(error_code const& ec, http_parser const& parser
			, char const* data, int size);
	
		int update_feed();
	
		aux::session_impl& session() const { return m_ses; }
	
		void set_settings(feed_settings const& s);
		void get_settings(feed_settings* s) const;
   	void get_feed_status(feed_status* ret) const;

		int next_update(time_t now) const;

		void load_state(lazy_entry const& rd);
		void save_state(entry& rd) const;
	
//	private:
	
		void add_item(feed_item const& item);

		feed_handle my_handle();

		error_code m_error;
		std::vector<feed_item> m_items;

		// these are all the URLs we've seen in the items list.
		// it's used to avoid adding duplicate entries to the actual
		// item vector
		std::set<std::string> m_urls;

		// these are URLs that have been added to the session
		// once. If we see them again, and they're not in the
		// session, don't add them again, since it means they
		// were removed from the session. It maps URLs to the
		// posix time when they were added. The timestamp is
		// used to prune this list by removing the oldest ones
		// when the size gets too big
		std::map<std::string, time_t> m_added;

		std::string m_title;
   	std::string m_description;
		time_t m_last_attempt;
   	time_t m_last_update;
		// refresh rate of this feed in minutes
		int m_ttl;
		// the number of update failures in a row
		int m_failures;
		// true while waiting for the server to respond
		bool m_updating;
		feed_settings m_settings;
	
		aux::session_impl& m_ses;
	};
	
};
	
#endif

