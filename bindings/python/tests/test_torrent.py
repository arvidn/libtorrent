import libtorrent as lt


def test_simimlar_torrent(torrent_info):
    from builtins import range

    torrents_info = [torrent_info for _ in range(10)]

    file_storage = lt.file_storage()

    for ti in torrents_info:
        for _file in ti.files():
            file_storage.add_file(_file.path, _file.size)

    file_storage.set_name('similar_torrent')

    create_torrent = lt.create_torrent(file_storage)

    for torrent_indice, ti in enumerate(torrents_info):
        create_torrent.add_similar_torrent(ti.info_hash())

        for file_indice, _file in enumerate(ti.files()):
            for piece_indice in range(ti.num_pieces()):
                create_torrent.set_hash(
                    piece_indice * torrent_indice,
                    ti.hash_for_piece(piece_indice)
                )
            create_torrent.set_file_hash(
                file_indice * torrent_indice,
                _file.filehash.to_bytes()
            )

    entry = create_torrent.generate()
    ti = lt.torrent_info(entry)

    assert ti.name() == 'similar_torrent'
    assert ti.info_hash()
