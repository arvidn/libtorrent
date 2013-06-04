/*

Copyright (c) 2013, Arvid Norberg
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

#include "libtorrent/alert_observer.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/thread.hpp"

#include <string>
#include <stdlib.h>
#include <set>

#include <boost/bind.hpp>
#include <algorithm>

namespace libtorrent
{
	class session;
	struct alert_handler;

	struct item_properties
	{
		int season;
		int episode;
		int quality;
		int source;

		enum quality_t
		{
			hd1080 = 1,
			hd720,
			sd
		};

		enum source_t
		{
			unknown,
			bluray,
			dvd,
			sattelite,
			tv,
			telesync,
			cam
		};
	};

	/**
		parses out season and episode numbers from a title.
		\param name the name of the torrent
		\param p the season, episode, source and quality
		is returned in this parameter.
	*/
	void parse_name(std::string name, item_properties& p);

	/**
		strips out all characters that are not alphanumerics (or dash).
		It also collapses any of such separator characters and replaces
		them with space. It also turns the string to lower case. The resulting
		string is a cleaned up representation of a title string.
	*/
	std::string normalize_title(std::string title);

	/**
		The rss_rule represents a set of matching criterias for
		an RSS feed item. Any matchin item, from any RSS feed
		is added to the session. The rules are added to the
		rss_filter_handler object.
	*/
	struct rss_rule
	{
		rss_rule() : id(0), exact_match(false), episode_filter(true) {}

		/// a unique identifier for this rule. This is initialized
		/// by the rss_filter_handler when adding the rule
		int id;

		/// only if this string is found in the name does
		/// this rule match
		std::string search;

		/// if this string is found in the name, it's not
		/// a hit
		std::string search_not;

		/// if not set, the torrent name is first
		/// normalized to lower case, non alpha-numerics
		/// are replaced by space and collapsed into
		/// single spaces
		bool exact_match;

		/// parse out season and episode and only add
		/// one of each episode
		bool episode_filter;

		/// for torrents that match this rule, these
		/// parameters are used to add the torrent to
		/// the session
		add_torrent_params params;
	};

	/**
		This is an alert_handler that listens for the
		rss_item_alert. The alert that's posted every
		time a new RSS item is received from any of the
		RSS feeds in the session.

		For every RSS feed item, the rules associated
		with the rss_filter_handler are evaluated, in
		order. Any rule that match will add the torrent
		to the session using the add_torrent_params
		of that rule, and terminate the search for further
		rules.
	*/
	struct rss_filter_handler : alert_observer
	{
		/// Pass in the alert_handler and the session object
		/// to subscribe to and to add the torrents to.
		rss_filter_handler(alert_handler& h, session& ses);
		~rss_filter_handler();

		/// retrieves the rule with the given id
		/// if no rule is found with the given id
		/// a default constructed rule is returned
		rss_rule get_rule(int id) const;

		/// returns all rules associated with this filter
		std::vector<rss_rule> get_rules() const;

		/// add a rule at the end of the rule list.
		/// returns the id assigned to this rule
		int add_rule(rss_rule const& r);

		/// updates the rule with the same id as r
		void edit_rule(rss_rule const& r);
		
		/// remove the rule with the specified id.
		void remove_rule(int id);

		// returns the number of rules
		int num_rules() const;

		virtual void handle_alert(alert const* a);

	private:
		// the mutex protects m_rules and associated state
		mutable mutex m_mutex;
		struct rss_rule_t : rss_rule
		{
			rss_rule_t(rss_rule const& r) : rss_rule(r) {}
			std::set<std::pair<int,int> > downloaded_episodes;
		};

		std::vector<rss_rule_t> m_rules;

		alert_handler& m_handler;
		session& m_ses;
		int m_next_id;
	};

}


