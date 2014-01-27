/*

Copyright (c) 2010, Arvid Norberg
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
	struct feed_settings
	{
		feed_settings()
			: auto_download(true)
			, auto_map_handles(true)
			, default_ttl(30)
		{}

   	std::string url;

		// automatically add torrents to session from
		bool auto_download;

		// automatically find existing torrents and set
		// the torrent_handle in the feed item
		bool auto_map_handles;

		// in minutes
		int default_ttl;

		// used when adding torrents
		add_torrent_params add_args;
	};

	struct feed_status
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

	boost::shared_ptr<feed> new_feed(aux::session_impl& ses, feed_settings const& sett);

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

