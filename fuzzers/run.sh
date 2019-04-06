
function run
{
# run for 48 hours
./fuzzers/${1} -max_total_time=172800 -timeout=10 -artifact_prefix=./${1}- corpus/${1}
}

run torrent_info &
run parse_magnet_uri &
run bdecode_node &
run lazy_bdecode &
run parse_int &
run sanitize_path &
run escape_path &
run file_storage_add_file &
run base32decode &
run base32encode &
run base64encode &
run escape_string e&
run gzip &
run verify_encoding &
run convert_to_native &
run convert_from_native &
run utf8_wchar &
run wchar_utf8 &
run utf8_codepoint &
run http_parser &
run upnp &
run dht_node &
run utp &
run resume_data &

wait
