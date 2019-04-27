from uuid import uuid4
import os

import pytest
import libtorrent as lt


@pytest.yield_fixture
def torrent_info(tmpdir):
    uid = str(uuid4())
    save_path = tmpdir.mkdir('save_path')
    directory_source_file = save_path.mkdir(uid)
    for _ in range(10):
        source_file = directory_source_file.join(uid + str(_))
        source_file.write(os.urandom(1024**2 * 10), mode='wb')  # 10M

    fs = lt.file_storage()
    lt.add_files(fs, str(directory_source_file))
    ct = lt.create_torrent(fs)
    lt.set_piece_hashes(ct, str(save_path))

    yield lt.torrent_info(ct.generate())

    try:
        save_path.remove()
    except Exception:
        pass
