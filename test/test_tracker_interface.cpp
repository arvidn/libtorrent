#include "test.hpp"
#include "libtorrent/aux_/mock_tracker_client.hpp"
#include "libtorrent/aux_/tracker_manager.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/settings_pack.hpp"
#include <future>

using namespace libtorrent;
using namespace libtorrent::aux;

TORRENT_TEST(tracker_interface_contract)
{
    io_context ios;
    settings_pack settings;

    auto client = std::make_unique<mock_tracker_client>(ios, settings);

    tracker_request req;
    req.url = "http://tracker.example.com/announce";
    req.info_hash = sha1_hash("01234567890123456789");

    std::promise<std::pair<error_code, tracker_response>> promise;
    auto future = promise.get_future();

    client->announce(req,
        [&promise](error_code const& ec, tracker_response const& resp) {
            promise.set_value({ec, resp});
        });

    ios.run_one();

    auto result = future.get();
    error_code ec = result.first;
    tracker_response resp = result.second;
    TEST_CHECK(!ec);  // Mock should succeed
}

TORRENT_TEST(tracker_announce_behavior)
{
    io_context ios;
    settings_pack settings;
    mock_tracker_client client(ios, settings);

    tracker_response expected_response;
    expected_response.interval = seconds(1800);
    expected_response.min_interval = seconds(900);
    expected_response.complete = 50;
    expected_response.incomplete = 10;

    client.set_mock_response(expected_response);

    tracker_request req;
    req.kind = {}; // announce_request (default)
    req.event = event_t::started;
    req.uploaded = 1024;
    req.downloaded = 2048;
    req.left = 4096;

    std::promise<tracker_response> promise;
    auto future = promise.get_future();

    client.announce(req,
        [&promise](error_code const& ec, tracker_response const& resp) {
            TEST_CHECK(!ec);
            promise.set_value(resp);
        });

    ios.run();

    auto resp = future.get();
    TEST_EQUAL(resp.interval.count(), expected_response.interval.count());
    TEST_EQUAL(resp.complete, expected_response.complete);
    TEST_EQUAL(resp.incomplete, expected_response.incomplete);
}

TORRENT_TEST(tracker_scrape_behavior)
{
    io_context ios;
    settings_pack settings;
    mock_tracker_client client(ios, settings);

    tracker_request req;
    req.kind = tracker_request::scrape_request;
    req.info_hash = sha1_hash("01234567890123456789");

    std::promise<tracker_response> promise;
    auto future = promise.get_future();

    client.scrape(req,
        [&promise](error_code const& ec, tracker_response const& resp) {
            TEST_CHECK(!ec);
            promise.set_value(resp);
        });

    ios.run();

    auto resp = future.get();
    TEST_CHECK(resp.complete >= 0);
    TEST_CHECK(resp.incomplete >= 0);
}

TORRENT_TEST(tracker_connection_reuse)
{
    io_context ios;
    settings_pack settings;
    mock_tracker_client client(ios, settings);

    TEST_CHECK(client.can_reuse());

    tracker_request req;
    client.announce(req, [](error_code const&, tracker_response const&) {});

    // Should still be reusable (mock doesn't close)
    TEST_CHECK(client.can_reuse());
}

TORRENT_TEST(tracker_close_behavior)
{
    io_context ios;
    settings_pack settings;
    mock_tracker_client client(ios, settings);

    TEST_CHECK(client.can_reuse());

    client.close();

    TEST_CHECK(!client.can_reuse());

    // Requests after close should fail
    tracker_request req;
    std::promise<error_code> promise;
    auto future = promise.get_future();

    client.announce(req,
        [&promise](error_code const& ec, tracker_response const& /*resp*/) {
            promise.set_value(ec);
        });

    ios.run_one();

    auto ec = future.get();
    TEST_CHECK(ec);  // Should have error
}

TORRENT_TEST(tracker_error_handling)
{
    io_context ios;
    settings_pack settings;
    mock_tracker_client client(ios, settings);

    client.set_mock_error(make_error_code(boost::system::errc::connection_refused));

    tracker_request req;
    std::promise<error_code> promise;
    auto future = promise.get_future();

    client.announce(req,
        [&promise](error_code const& ec, tracker_response const&) {
            promise.set_value(ec);
        });

    ios.run();

    auto ec = future.get();
    TEST_CHECK(ec);
    TEST_EQUAL(ec, make_error_code(boost::system::errc::connection_refused));
}

TORRENT_TEST(tracker_async_operations)
{
    io_context ios;
    settings_pack settings;
    mock_tracker_client client(ios, settings);

    client.set_mock_delay(milliseconds(100));

    auto start = clock_type::now();

    tracker_request req;
    std::promise<void> promise;
    auto future = promise.get_future();

    client.announce(req,
        [&promise](error_code const&, tracker_response const&) {
            promise.set_value();
        });

    // Should return immediately (async)
    auto immediate = clock_type::now() - start;
    TEST_CHECK(immediate < milliseconds(10));

    ios.run();
    future.wait();

    // Callback should happen after delay
    auto total = clock_type::now() - start;
    TEST_CHECK(total >= milliseconds(100));
}