/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>

#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/disabled_disk_io.hpp>
#include <libtorrent/write_resume_data.hpp> // for write_torrent_file

int main(int argc, char const* argv[]) try
{
	if (argc != 3) {
		std::cerr << "usage: " << argv[0] << " <magnet-url> <output torrent file>" << std::endl;
		return 1;
	}
	lt::session_params params;
	params.disk_io_constructor = lt::disabled_disk_io_constructor;

	params.settings.set_int(lt::settings_pack::alert_mask
		, lt::alert_category::status | lt::alert_category::error);

	lt::session ses(std::move(params));

	lt::add_torrent_params atp = lt::parse_magnet_uri(argv[1]);
	atp.save_path = ".";
	atp.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
	atp.file_priorities.resize(100, lt::dont_download);

	std::this_thread::sleep_for(std::chrono::seconds(1));
	lt::torrent_handle h = ses.add_torrent(std::move(atp));

	for (;;) {
		std::vector<lt::alert*> alerts;
		ses.pop_alerts(&alerts);

		for (lt::alert* a : alerts)
		{
			std::cout << a->message() << std::endl;
			if (auto const* mra = lt::alert_cast<lt::metadata_received_alert>(a))
			{
				std::cerr << "metadata received" << std::endl;
				auto const handle = mra->handle;
				std::shared_ptr<lt::torrent_info const> ti = handle.torrent_file();
				if (!ti)
				{
					std::cerr << "unexpected missing torrent info" << std::endl;
					goto done;
				}

				// in order to create valid v2 torrents, we need to download the
				// piece hashes. libtorrent currently only downloads the hashes
				// on-demand, so we would have to download all the content.
				// Instead, produce an invalid v2 torrent that's missing piece
				// layers
				if (ti->v2())
				{
					std::cerr << "found v2 torrent, generating a torrent missing piece hashes" << std::endl;
				}
				handle.save_resume_data(lt::torrent_handle::save_info_dict);
				handle.set_flags(lt::torrent_flags::paused);
			}
			else if (auto* rda = lt::alert_cast<lt::save_resume_data_alert>(a))
			{
				// don't include piece layers
				rda->params.merkle_trees.clear();
				lt::entry e = lt::write_torrent_file(rda->params, lt::write_flags::allow_missing_piece_layer);
				std::vector<char> torrent;
				lt::bencode(std::back_inserter(torrent), e);
				std::ofstream out(argv[2]);
				out.write(torrent.data(), int(torrent.size()));
				goto done;
			}
			else if (auto const* rdf = lt::alert_cast<lt::save_resume_data_failed_alert>(a))
			{
				std::cerr << "failed to save resume data: " << rdf->message() << std::endl;
				goto done;
			}
		}
		ses.wait_for_alert(std::chrono::milliseconds(200));
	}
	done:
	std::cerr<< "done, shutting down" << std::endl;
}
catch (std::exception& e)
{
	std::cerr << "Error: " << e.what() << std::endl;
}
