import os
import shutil
import hashlib

corpus_dirs = [
    'torrent_info', 'upnp', 'gzip' 'base32decode', 'base32encode',
    'base64encode', 'bdecode_node', 'convert_from_native', 'convert_to_native',
    'dht_node', 'escape_path', 'escape_string', 'file_storage_add_file', 'gzip',
    'http_parser', 'lazy_bdecode', 'parse_int', 'parse_magnet_uri', 'resume_data',
    'sanitize_path', 'torrent_info', 'upnp', 'utf8_codepoint', 'utf8_wchar', 'utp',
    'verify_encoding', 'wchar_utf8']

for p in corpus_dirs:
    try:
        os.makedirs(os.path.join('corpus', p))
    except Exception as e:
        print(e)

torrent_dir = '../test/test_torrents'
for f in os.listdir(torrent_dir):
    shutil.copy(os.path.join(torrent_dir, f), os.path.join('corpus', 'torrent_info'))

xml_tests = [
    '<a blah="b"></a>', '<a b=c></a>', '<a b"c"></a>', '<a b="c></a>',
    '<![CDATA[<sender>John Smith</sender>]]>', '<![CDATA[<sender>John S',
    '<!-- comment -->', '<empty></empty>', '<tag',
    '''<?xml version="1.0" encoding="ISO-8859-1" ?>
<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema"></xs:schema>''',
    '<selfclosing />']

for x in xml_tests:
    name = hashlib.sha1(x).hexdigest()
    with open(os.path.join('corpus', 'upnp', name), 'w+') as f:
        f.write(x)

gzip_dir = '../test'
for f in ['zeroes.gz', 'corrupt.gz', 'invalid1.gz']:
    shutil.copy(os.path.join(gzip_dir, f), os.path.join('corpus', 'gzip'))
