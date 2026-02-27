https://en.wikipedia.org/wiki/Torrent_file#HTTP_seeds

HTTP seeds

BEP-0019 is one of two extensions allowing HTTP seeds to be used in BitTorrent.

In BEP-0019, a new key url-list, is placed in the top-most list.
The client uses the links to assemble ordinary HTTP URLs – no server-side support is required.
This feature is very commonly used by open source projects offering software downloads.
Web seeds allow smart selection and simultaneous use of mirror sites, P2P or HTTP(S), by the client.
Doing so reducing the load on the project's servers while maximizing download speed.
MirrorBrain [de] automatically generates torrents with web seeds.



https://bittorrent.org/beps/bep_0019.html

WebSeed - HTTP/FTP Seeding (GetRight style)



libtorrent/src/web_peer_connection.cpp

bep_0019



https://en.wikipedia.org/wiki/Torrent_file#HTTP_seeds_2

HTTP seeds

BEP-0017 extends BitTorrent to support HTTP seeds, later more commonly termed "web seeds" to be inclusive of HTTPS.

In BEP-0017, a new key, httpseeds, is placed in the top-most list (i.e., with announce and info). This key's value is a list of web addresses where torrent data can be retrieved. Special server support is required. It remains at Draft status.

{
  # ...
  'httpseeds': ['http://www.site1.com/source1.php', 'http://www.site2.com/source2.php'],
  # ...
}



https://bittorrent.org/beps/bep_0017.html

HTTP Seeding

Protocol

The client calls the URL given, in the following format:

<url>?info_hash=[hash]&piece=[piece]{&ranges=[start]-[end]{,[start]-[end]}...}



libtorrent/src/http_seed_connection.cpp

bep_0017
