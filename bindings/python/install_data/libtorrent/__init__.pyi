import datetime
from typing import Any
from typing import Callable
from typing import Dict
from typing import Iterable
from typing import Iterator
from typing import List
from typing import Mapping
from typing import Optional
from typing import overload
from typing import Tuple
from typing import Union

from typing_extensions import TypedDict

# I created this file by starting with stubgen and modifying its output.

# I recommend that maintainers occasionally re-run stubgen, and compare it to
# this file. This diff will be too verbose to use for small changes, but it's the
# only way to ensure coverage.

# To help with the above process, please keep everything in sorted order, to
# match stubgen's output.

# Typing notes:
# - This project has a very particular approach to typed enum values. Pay
#   special attention to which are typed, and which are just ints.
# - Some attributes like add_torrent_params.save_path ought to be read as one
#   type (str) and set as another (Union[str, bytes]), but mypy doesn't support
#   this.
# - We use strict primitive types (Dict and List) for function args, because
#   boost strictly requires these, and will not understand duck-typed
#   abstractions like Mapping or Sequence.
# - I chose to make the "names" and "values" enum class attributes typed as
#   Mapping, specifically to block modification. I coludn't think of a case
#   where these would need to be used in a Dict context.
# - Boost mangles function signatures to all look like (*args, **kwargs) in
#   python. This allows for "anonymous" positional args which python doesn't
#   normally support. We want the type stubs to match the *usage* of a function,
#   not this mangled version, so for anonymous positional args, we use a name
#   like "_name" (these names might fairly all be "_", but python doesn't allow
#   duplicates). PEP 570 provides positional-only argument syntax but mypy
#   doesn't support it as of writing.

_PathLike = Union[str, bytes]
_Entry = Union[bytes, int, List, Dict[bytes, Any], Tuple[int, ...]]

create_smart_ban_plugin: str
create_ut_metadata_plugin: str
create_ut_pex_plugin: str
version: str
version_major: int
version_minor: int
__version__: str

@overload
def add_files(fs: file_storage, path: _PathLike, flags: int = ...) -> None: ...
@overload
def add_files(
    fs: file_storage,
    path: _PathLike,
    predicate: Callable[[str], Any],
    flags: int = ...,
) -> None: ...
def add_magnet_uri(
    _session: session, _uri: str, _limits: Dict[str, Any]
) -> torrent_handle: ...
def bdecode(_bencoded: bytes) -> _Entry: ...
def bdecode_category() -> error_category: ...
def bencode(_bdecoded: _Entry) -> bytes: ...
def client_fingerprint(_pid: sha1_hash) -> Optional[fingerprint]: ...

# We could have this return Mapping, but this would interfere with using it as
# input elsewhere where Dict is strictly expected
def default_settings() -> Dict[str, Any]: ...
def find_metric_idx(_metric_name: str) -> int: ...
def generate_fingerprint(
    _prefix: Union[str, bytes], _major: int, _minor: int, _rev: int, _tag: int
) -> str: ...
def generate_fingerprint_bytes(
    name: bytes, major: int, minor: int = ..., revision: int = ..., tag: int = ...
) -> bytes: ...
def generic_category() -> error_category: ...
def get_bdecode_category() -> error_category: ...
def get_http_category() -> error_category: ...
def get_i2p_category() -> error_category: ...
def get_libtorrent_category() -> error_category: ...
def get_socks_category() -> error_category: ...
def get_upnp_category() -> error_category: ...
def high_performance_seed() -> Dict[str, Any]: ...
def http_category() -> error_category: ...
def i2p_category() -> error_category: ...
def identify_client(_pid: sha1_hash) -> str: ...
def libtorrent_category() -> error_category: ...
@overload
def make_magnet_uri(_handle: torrent_handle) -> str: ...
@overload
def make_magnet_uri(_ti: torrent_info) -> str: ...
def min_memory_usage() -> Dict[str, Any]: ...
def operation_name(_op: operation_t) -> str: ...
def parse_magnet_uri(_uri: str) -> add_torrent_params: ...

# Note that we include every param supported by add_torrent() here, though
# this type is only created by parse_magnet_uri_dict() which doesn't populate
# everything. This is to support users modifying its output before passing to
# add_torrent().
class _AddTorrentParamsDict(TypedDict, total=False):
    ti: torrent_info
    trackers: List[str]
    dht_nodes: List[Tuple[str, int]]
    info_hashes: bytes
    info_hash: bytes
    name: str
    save_path: str
    storage_mode: storage_mode_t
    url: str
    flags: int
    resume_data: bytes
    http_seeds: List[str]
    banned_peers: List[Tuple[str, int]]
    peers: List[Tuple[str, int]]
    trackerid: str
    renamed_files: Dict[int, str]
    file_priorities: List[int]

def parse_magnet_uri_dict(_uri: str) -> _AddTorrentParamsDict: ...
@overload
def read_resume_data(_bencoded: bytes) -> add_torrent_params: ...
@overload
def read_resume_data(
    _bencoded: bytes, _limits: Dict[str, Any]
) -> add_torrent_params: ...
@overload
def read_session_params(buffer: bytes, flags: int = ...) -> session_params: ...
@overload
def read_session_params(dict: Dict[bytes, Any], flags: int = ...) -> session_params: ...
def session_stats_metrics() -> List[stats_metric]: ...
@overload
def set_piece_hashes(
    _ct: create_torrent, _path: _PathLike, _callback: Callable[[int], Any]
) -> None: ...
@overload
def set_piece_hashes(_ct: create_torrent, _path: _PathLike) -> None: ...
@overload
def list_files(p: _PathLike, flags: int = ...) -> List[create_file_entry]: ...
@overload
def list_files(
    p: _PathLike, cb: Callable[[str], bool], flags: int = ...
) -> List[create_file_entry]: ...
def socks_category() -> error_category: ...
def system_category() -> error_category: ...
def upnp_category() -> error_category: ...
def write_resume_data(_atp: add_torrent_params) -> Dict[bytes, Any]: ...
def write_resume_data_buf(_atp: add_torrent_params) -> bytes: ...
def write_session_params(
    entry: session_params, flags: int = ...
) -> Dict[bytes, Any]: ...

class add_piece_flags_t:
    overwrite_existing: int

class add_torrent_alert(torrent_alert):
    error: error_code
    params: add_torrent_params

class add_torrent_params:
    active_time: int
    added_time: int
    banned_peers: List[Tuple[str, int]]
    completed_time: int
    dht_nodes: List[Tuple[str, int]]
    download_limit: int
    file_priorities: List[int]
    finished_time: int
    flags: int
    have_pieces: List[bool]
    http_seeds: List[str]
    info_hash: sha1_hash
    info_hashes: info_hash_t
    last_download: int
    last_seen_complete: int
    last_upload: int
    max_connections: int
    max_uploads: int
    merkle_tree: List[sha1_hash]
    name: str
    num_complete: int
    num_downloaded: int
    num_incomplete: int
    peers: List[Tuple[str, int]]
    piece_priorities: List[int]
    renamed_files: Dict[int, str]
    resume_data: List[str]
    save_path: str
    seeding_time: int
    storage_mode: storage_mode_t
    ti: Optional[torrent_info]
    total_downloaded: int
    total_uploaded: int
    tracker_tiers: List[int]
    trackerid: str
    trackers: List[str]
    unfinished_pieces: Dict[int, List[bool]]
    upload_limit: int
    url: str
    url_seeds: List[str]
    verified_pieces: List[bool]
    version: int

class add_torrent_params_flags_t:
    default_flags: int
    flag_apply_ip_filter: int
    flag_auto_managed: int
    flag_duplicate_is_error: int
    flag_merge_resume_http_seeds: int
    flag_merge_resume_trackers: int
    flag_override_resume_data: int
    flag_override_trackers: int
    flag_override_web_seeds: int
    flag_paused: int
    flag_pinned: int
    flag_seed_mode: int
    flag_sequential_download: int
    flag_share_mode: int
    flag_stop_when_ready: int
    flag_super_seeding: int
    flag_update_subscribe: int
    flag_upload_mode: int
    flag_use_resume_save_path: int

class alert:
    class category_t:
        all_categories: int
        block_progress_notification: int
        connect_notification: int
        debug_notification: int
        dht_log_notification: int
        dht_notification: int
        dht_operation_notification: int
        error_notification: int
        file_progress_notification: int
        incoming_request_notification: int
        ip_block_notification: int
        peer_log_notification: int
        peer_notification: int
        performance_warning: int
        picker_log_notification: int
        piece_progress_notification: int
        port_mapping_log_notification: int
        port_mapping_notification: int
        progress_notification: int
        session_log_notification: int
        stats_notification: int
        status_notification: int
        storage_notification: int
        torrent_log_notification: int
        tracker_notification: int
        upload_notification: int
    def category(self) -> int: ...
    def message(self) -> str: ...
    def what(self) -> str: ...

class alert_category:
    all: int
    block_progress: int
    connect: int
    dht: int
    dht_log: int
    dht_operation: int
    error: int
    file_progress: int
    incoming_request: int
    ip_block: int
    peer: int
    peer_log: int
    performance_warning: int
    picker_log: int
    piece_progress: int
    port_mapping: int
    port_mapping_log: int
    session_log: int
    stats: int
    status: int
    storage: int
    torrent_log: int
    tracker: int
    upload: int

class alerts_dropped_alert(alert):
    dropped_alerts: List[bool]

class announce_entry:
    def __init__(self, url: str) -> None: ...
    def can_announce(self, _is_seed: bool) -> bool: ...
    def is_working(self) -> bool: ...
    def min_announce_in(self) -> int: ...
    def next_announce_in(self) -> int: ...
    def reset(self) -> None: ...
    def trim(self) -> None: ...
    complete_sent: bool
    fail_limit: int
    fails: int
    last_error: error_code
    message: str
    min_announce: Optional[datetime.datetime]
    next_announce: Optional[datetime.datetime]
    scrape_complete: int
    scrape_downloaded: int
    scrape_incomplete: int
    send_stats: bool
    source: int
    start_sent: bool
    tier: int
    trackerid: str
    updating: bool
    url: str
    verified: bool

class announce_flags_t:
    seed: int
    implied_port: int
    ssl_torrent: int

class anonymous_mode_alert(torrent_alert):
    kind: int
    str: str

class bandwidth_mixed_algo_t(int):
    names: Mapping[str, bandwidth_mixed_algo_t]
    peer_proportional: bandwidth_mixed_algo_t
    prefer_tcp: bandwidth_mixed_algo_t
    values: Mapping[int, bandwidth_mixed_algo_t]

class block_downloading_alert(peer_alert):
    block_index: int
    peer_speedmsg: str
    piece_index: int

class block_finished_alert(peer_alert):
    block_index: int
    piece_index: int

class block_timeout_alert(peer_alert):
    block_index: int
    piece_index: int

class block_uploaded_alert(peer_alert):
    block_index: int
    piece_index: int

class cache_flushed_alert(torrent_alert):
    pass

class choking_algorithm_t(int):
    auto_expand_choker: choking_algorithm_t
    bittyrant_choker: choking_algorithm_t
    fixed_slots_choker: choking_algorithm_t
    names: Mapping[str, choking_algorithm_t]
    rate_based_choker: choking_algorithm_t
    values: Mapping[int, choking_algorithm_t]

class close_reason_t(int):
    blocked: close_reason_t
    corrupt_pieces: close_reason_t
    duplicate_peer_id: close_reason_t
    encryption_error: close_reason_t
    invalid_allow_fast_message: close_reason_t
    invalid_bitfield_message: close_reason_t
    invalid_cancel_message: close_reason_t
    invalid_choke_message: close_reason_t
    invalid_dht_port_message: close_reason_t
    invalid_dont_have_message: close_reason_t
    invalid_extended_message: close_reason_t
    invalid_have_all_message: close_reason_t
    invalid_have_message: close_reason_t
    invalid_have_none_message: close_reason_t
    invalid_info_hash: close_reason_t
    invalid_interested_message: close_reason_t
    invalid_message: close_reason_t
    invalid_message_id: close_reason_t
    invalid_metadata: close_reason_t
    invalid_metadata_message: close_reason_t
    invalid_metadata_offset: close_reason_t
    invalid_metadata_request_message: close_reason_t
    invalid_not_interested_message: close_reason_t
    invalid_pex_message: close_reason_t
    invalid_piece_message: close_reason_t
    invalid_reject_message: close_reason_t
    invalid_request_message: close_reason_t
    invalid_suggest_message: close_reason_t
    invalid_unchoke_message: close_reason_t
    message_too_big: close_reason_t
    metadata_too_big: close_reason_t
    names: Mapping[str, close_reason_t]
    no_memory: close_reason_t
    none: close_reason_t
    not_interested_upload_only: close_reason_t
    peer_churn: close_reason_t
    pex_message_too_big: close_reason_t
    pex_too_frequent: close_reason_t
    port_blocked: close_reason_t
    protocol_blocked: close_reason_t
    request_when_choked: close_reason_t
    self_connection: close_reason_t
    timed_out_activity: close_reason_t
    timed_out_handshake: close_reason_t
    timed_out_interest: close_reason_t
    timed_out_request: close_reason_t
    timeout: close_reason_t
    too_many_connections: close_reason_t
    too_many_files: close_reason_t
    torrent_removed: close_reason_t
    upload_to_upload: close_reason_t
    values: Mapping[int, close_reason_t]

class create_file_entry:
    def __init__(
        self,
        filename: str,
        size: int,
        flags: int = ...,
        mtime: int = ...,
        symlink: str = ...,
    ) -> None: ...
    filename: str
    size: int
    flags: int
    mtime: int
    symlink: str

class create_torrent:
    canonical_files: int
    merkle: int
    modification_time: int
    optimize_alignment: int
    symlinks: int
    v1_only: int
    v2_only: int
    @overload
    def __init__(
        self, files: List[create_file_entry], piece_size: int = ..., flags: int = ...
    ) -> None: ...
    @overload
    def __init__(
        self, storage: file_storage, piece_size: int = ..., flags: int = ...
    ) -> None: ...
    @overload
    def __init__(self, _fs: file_storage) -> None: ...
    @overload
    def __init__(self, ti: torrent_info) -> None: ...
    def add_collection(self, _collection: str) -> None: ...
    def add_http_seed(self, _url: str) -> None: ...
    def add_node(self, _ip: str, _port: int) -> None: ...
    def add_similar_torrent(self, _info_hash: sha1_hash) -> None: ...
    def add_tracker(self, announce_url: str, tier: int = ...) -> None: ...
    def add_url_seed(self, _url: str) -> None: ...
    def files(self) -> file_storage: ...
    def generate(self) -> Dict[bytes, Any]: ...
    def num_pieces(self) -> int: ...
    def piece_length(self) -> int: ...
    def piece_size(self, _index: int) -> int: ...
    def priv(self) -> bool: ...
    def set_comment(self, _comment: str) -> None: ...
    def set_creator(self, _creator: str) -> None: ...
    def set_file_hash(self, _index: int, _hash: bytes) -> None: ...
    def set_hash(self, _index: int, _hash: bytes) -> None: ...
    def set_priv(self, _priv: bool) -> None: ...
    # TODO: is this right?
    def set_root_cert(self, pem: str) -> None: ...

class create_torrent_flags_t:
    merkle: int
    modification_time: int
    optimize: int
    optimize_alignment: int
    symlinks: int
    v2_only: int

class deadline_flags_t:
    alert_when_available: int

class deprecated_move_flags_t(int):
    always_replace_files: deprecated_move_flags_t
    dont_replace: deprecated_move_flags_t
    fail_if_exist: deprecated_move_flags_t
    names: Mapping[str, deprecated_move_flags_t]
    values: Mapping[int, deprecated_move_flags_t]

class dht_announce_alert(alert):
    info_hash: sha1_hash
    ip: str
    port: int

class dht_bootstrap_alert(alert):
    pass

class dht_get_peers_alert(alert):
    info_hash: sha1_hash

class dht_get_peers_reply_alert(alert):
    def num_peers(self) -> int: ...
    def peers(self) -> List[Tuple[str, int]]: ...
    info_hash: sha1_hash

class dht_immutable_item_alert(alert):
    item: Optional[_Entry]
    target: sha1_hash

class _DhtNodeDict(TypedDict):
    nid: sha1_hash
    endpoint: Tuple[str, int]

class dht_live_nodes_alert(alert):
    node_id: sha1_hash
    nodes: List[_DhtNodeDict]
    num_nodes: int

class dht_log_alert(alert):
    def log_message(self) -> str: ...
    module: int

class dht_lookup:
    branch_factor: int
    outstanding_requests: int
    response: int
    timeouts: int
    type: Optional[str]

class dht_module_t:
    names: Mapping[str, dht_module_t]
    node: dht_module_t
    routing_table: dht_module_t
    rpc_manager: dht_module_t
    tracker: dht_module_t
    traversal: dht_module_t
    values: Mapping[int, dht_module_t]

class dht_mutable_item_alert(alert):
    authoritative: bool
    item: Optional[_Entry]
    key: bytes
    salt: bytes
    seq: int
    signature: bytes

class dht_outgoing_get_peers_alert(alert):
    endpoint: Tuple[str, int]
    info_hash: sha1_hash
    ip: Tuple[str, int]
    obfuscated_info_hash: sha1_hash

class dht_pkt_alert(alert):
    pkt_buf: bytes

class dht_put_alert(alert):
    num_success: int
    public_key: bytes
    salt: bytes
    seq: int
    signature: bytes
    target: sha1_hash

class dht_reply_alert(tracker_alert):
    num_peers: int

class dht_sample_infohashes_alert(alert):
    endpoint: Tuple[str, int]
    interval: datetime.timedelta
    nodes: List[_DhtNodeDict]
    num_infohashes: int
    num_nodes: int
    num_samples: int
    samples: List[sha1_hash]

class dht_settings:
    aggressive_lookups: bool
    block_ratelimit: int
    block_timeout: int
    enforce_node_id: bool
    extended_routing_table: bool
    ignore_dark_internet: bool
    item_lifetime: int
    max_dht_items: int
    max_fail_count: int
    max_peers_reply: int
    max_torrent_search_reply: int
    max_torrents: int
    privacy_lookups: bool
    read_only: bool
    restrict_routing_ips: bool
    restrict_search_ips: bool
    search_branching: int

class dht_state:
    nids: List[Tuple[str, sha1_hash]]
    nodes: List[Tuple[str, int]]
    nodes6: List[Tuple[str, int]]

class _DhtStatsActiveRequest(TypedDict):
    type: Optional[str]
    outstanding_requests: int
    timeouts: int
    responses: int
    branch_factor: int
    nodes_left: int
    last_sent: int
    first_timeout: int

class _DhtStatsRoute(TypedDict):
    num_nodes: int
    num_replacements: int

class dht_stats_alert(alert):
    active_requests: List[_DhtStatsActiveRequest]
    routing_table: List[_DhtStatsRoute]

class enc_level(int):
    both: enc_level
    names: Mapping[str, enc_level]
    pe_both: enc_level
    pe_plaintext: enc_level
    pe_rc4: enc_level
    plaintext: enc_level
    rc4: enc_level
    values: Mapping[int, enc_level]

class enc_policy(int):
    disabled: enc_policy
    enabled: enc_policy
    forced: enc_policy
    names: Mapping[str, enc_policy]
    pe_disabled: enc_policy
    pe_enabled: enc_policy
    pe_forced: enc_policy
    values: Mapping[int, enc_policy]

class error_category:
    def message(self, _value: int) -> str: ...
    def name(self) -> str: ...

class error_code:
    @overload
    def __init__(self) -> None: ...
    @overload
    def __init__(self, _value: int, _cat: error_category) -> None: ...
    def assign(self, _value: int, _cat: error_category) -> None: ...
    def category(self) -> error_category: ...
    def clear(self) -> None: ...
    def message(self) -> str: ...
    def value(self) -> int: ...

class event_t(int):
    completed: event_t
    names: Mapping[str, event_t]
    none: event_t
    paused: event_t
    started: event_t
    stopped: event_t
    values: Mapping[int, event_t]

class external_ip_alert(alert):
    external_address: str

class fastresume_rejected_alert(torrent_alert):
    def file_path(self) -> str: ...
    error: error_code
    msg: str
    op: operation_t
    operation: str

class file_completed_alert(torrent_alert):
    index: int

class file_entry:
    executable_attribute: bool
    filehash: sha1_hash
    hidden_attribute: bool
    mtime: int
    offset: int
    pad_file: bool
    path: str
    size: int
    symlink_attribute: bool
    symlink_path: str

class file_error_alert(torrent_alert):
    def filename(self) -> str: ...
    error: error_code
    file: str
    msg: str

class file_flags_t:
    flag_executable: int
    flag_hidden: int
    flag_pad_file: int
    flag_symlink: int

class file_open_mode:
    locked: int
    no_atime: int
    random_access: int
    read_only: int
    read_write: int
    rw_mask: int
    sparse: int
    write_only: int

class file_prio_alert(torrent_alert):
    pass

class file_progress_flags_t:
    piece_granularity: int

class file_rename_failed_alert(torrent_alert):
    error: error_code
    index: int

class file_renamed_alert(torrent_alert):
    def new_name(self) -> str: ...
    def old_name(self) -> str: ...
    index: int
    name: str

class file_slice:
    file_index: int
    offset: int
    size: int

class file_storage:
    flag_executable: int
    flag_hidden: int
    flag_pad_file: int
    flag_symlink: int
    @overload
    def add_file(self, entry: file_entry) -> None: ...
    @overload
    def add_file(
        self,
        path: str,
        size: int,
        flags: int = ...,
        mtime: int = ...,
        linkpath: str = ...,
    ) -> None: ...
    def at(self, _index: int) -> file_entry: ...
    def file_flags(self, _index: int) -> int: ...
    def file_name(self, _index: int) -> str: ...
    def file_offset(self, _index: int) -> int: ...
    def file_path(self, idx: int, save_path: str = ...) -> str: ...
    def file_size(self, _index: int) -> int: ...
    def hash(self, _index: int) -> sha1_hash: ...
    def is_valid(self) -> bool: ...
    def name(self) -> str: ...
    def num_files(self) -> int: ...
    def num_pieces(self) -> int: ...
    def piece_length(self) -> int: ...
    def piece_size(self, _index: int) -> int: ...
    def rename_file(self, _index: int, _path: str) -> None: ...
    def set_name(self, _path: str) -> None: ...
    def set_num_pieces(self, _num: int) -> None: ...
    def set_piece_length(self, _len: int) -> None: ...
    def size_on_disk(self) -> int: ...
    def symlink(self, _index: int) -> str: ...
    def total_size(self) -> int: ...
    def __iter__(self) -> Iterator[file_entry]: ...
    def __len__(self) -> int: ...

class fingerprint:
    def __init__(
        self, _prefix: str, _maj: int, _min: int, _rev: int, _tag: int
    ) -> None: ...
    major_version: int
    minor_version: int
    revision_version: int
    tag_version: int

class hash_failed_alert(torrent_alert):
    piece_index: int

class i2p_alert(alert):
    error: error_code

class incoming_connection_alert(alert):
    endpoint: Tuple[str, int]
    ip: Tuple[str, int]
    socket_type: socket_type_t

class info_hash_t:
    @overload
    def __init__(self) -> None: ...
    @overload
    def __init__(self, sha1_hash: sha1_hash) -> None: ...
    @overload
    def __init__(self, sha256_hash: sha256_hash) -> None: ...
    @overload
    def __init__(self, sha1_hash: sha1_hash, sha256_hash: sha256_hash) -> None: ...
    def get(self, _v: protocol_version) -> sha1_hash: ...
    def get_best(self) -> sha1_hash: ...
    def has(self, _v: protocol_version) -> bool: ...
    def has_v1(self) -> bool: ...
    def has_v2(self) -> bool: ...
    def __eq__(self, other: Any) -> bool: ...
    def __lt__(self, other: Any) -> bool: ...
    def __ne__(self, other: Any) -> bool: ...
    v1: sha1_hash
    v2: sha256_hash

class invalid_request_alert(peer_alert):
    request: peer_request

class io_buffer_mode_t(int):
    disable_os_cache: int
    disable_os_cache_for_aligned_files: int
    enable_os_cache: int
    names: Mapping[str, io_buffer_mode_t]
    values: Mapping[int, io_buffer_mode_t]

class ip_filter:
    def access(self, _ip: str) -> int: ...
    def add_rule(self, _start: str, _stop: str, _flag: int) -> None: ...
    def export_filter(self) -> Tuple[List[Tuple[str, str]], List[Tuple[str, str]]]: ...

class kind(int):
    names: Mapping[str, kind]
    tracker_no_anonymous: kind
    values: Mapping[int, kind]

class listen_failed_alert(alert):
    def listen_interface(self) -> str: ...
    address: str
    endpoint: Tuple[str, int]
    error: error_code
    op: operation_t
    operation: int
    port: int
    sock_type: listen_failed_alert_socket_type_t
    socket_type: socket_type_t

class listen_failed_alert_socket_type_t(int):
    i2p: listen_failed_alert_socket_type_t
    names: Mapping[str, listen_failed_alert_socket_type_t]
    socks5: listen_failed_alert_socket_type_t
    tcp: listen_failed_alert_socket_type_t
    tcp_ssl: listen_failed_alert_socket_type_t
    udp: listen_failed_alert_socket_type_t
    utp_ssl: listen_failed_alert_socket_type_t
    values: Mapping[int, listen_failed_alert_socket_type_t]

class listen_on_flags_t(int):
    listen_no_system_port: listen_on_flags_t
    listen_reuse_address: listen_on_flags_t
    names: Mapping[str, listen_on_flags_t]
    values: Mapping[int, listen_on_flags_t]

class listen_succeded_alert_socket_type_t(int):
    i2p: listen_succeded_alert_socket_type_t
    names: Mapping[str, listen_succeded_alert_socket_type_t]
    socks5: listen_succeded_alert_socket_type_t
    tcp: listen_succeded_alert_socket_type_t
    tcp_ssl: listen_succeded_alert_socket_type_t
    udp: listen_succeded_alert_socket_type_t
    utp_ssl: listen_succeded_alert_socket_type_t
    values: Mapping[int, listen_succeded_alert_socket_type_t]

class listen_succeeded_alert(alert):
    address: str
    endpoint: Tuple[str, int]
    port: int
    sock_type: listen_succeded_alert_socket_type_t
    socket_type: socket_type_t

class log_alert(alert):
    def log_message(self) -> str: ...
    def msg(self) -> str: ...

class lsd_error_alert(alert):
    error: error_code

class metadata_failed_alert(torrent_alert):
    error: error_code

class metadata_received_alert(torrent_alert):
    pass

class metric_type_t(int):
    counter: metric_type_t
    gauge: metric_type_t
    names: Mapping[str, metric_type_t]
    values: Mapping[int, metric_type_t]

class move_flags_t(int):
    always_replace_files: move_flags_t
    dont_replace: move_flags_t
    fail_if_exist: move_flags_t
    names: Mapping[str, move_flags_t]
    values: Mapping[int, move_flags_t]

class open_file_state:
    file_index: int
    # last_use: broken
    open_mode: file_open_mode

class operation_t(int):
    alloc_cache_piece: operation_t
    alloc_recvbuf: operation_t
    alloc_sndbuf: operation_t
    available: operation_t
    bittorrent: operation_t
    check_resume: operation_t
    connect: operation_t
    encryption: operation_t
    enum_if: operation_t
    exception: operation_t
    file: Any = ...
    file_copy: operation_t
    file_fallocate: operation_t
    file_hard_link: operation_t
    file_open: operation_t
    file_read: operation_t
    file_remove: operation_t
    file_rename: operation_t
    file_stat: operation_t
    file_write: operation_t
    get_interface: operation_t
    getname: operation_t
    getpeername: operation_t
    handshake: operation_t
    hostname_lookup: operation_t
    iocontrol: operation_t
    mkdir: operation_t
    names: Mapping[str, operation_t]
    parse_address: operation_t
    partfile_move: operation_t
    partfile_read: operation_t
    partfile_write: operation_t
    sock_accept: operation_t
    sock_bind: operation_t
    sock_bind_to_device: operation_t
    sock_listen: operation_t
    sock_open: operation_t
    sock_option: operation_t
    sock_read: operation_t
    sock_write: operation_t
    ssl_handshake: operation_t
    symlink: operation_t
    unknown: operation_t
    values: Mapping[int, operation_t]

class options_t:
    delete_files: int

class pause_flags_t:
    graceful_pause: int

class pe_settings:
    allowed_enc_level: int
    in_enc_policy: int
    out_enc_policy: int
    prefer_rc4: bool

class peer_alert(torrent_alert):
    endpoint: Tuple[str, int]
    ip: Tuple[str, int]
    pid: sha1_hash

class peer_ban_alert(peer_alert):
    pass

class peer_blocked_alert(peer_alert):
    reason: reason_t

class peer_class_type_filter:
    i2p_socket: peer_class_type_filter_socket_type_t
    ssl_tcp_socket: peer_class_type_filter_socket_type_t
    ssl_utp_socket: peer_class_type_filter_socket_type_t
    tcp_socket: peer_class_type_filter_socket_type_t
    utp_socket: peer_class_type_filter_socket_type_t
    def add(self, _type: peer_class_type_filter_socket_type_t, _class: int) -> None: ...
    def allow(
        self, _type: peer_class_type_filter_socket_type_t, _class: int
    ) -> None: ...
    def apply(
        self, _type: peer_class_type_filter_socket_type_t, _class: int
    ) -> int: ...
    def disallow(
        self, _type: peer_class_type_filter_socket_type_t, _class: int
    ) -> None: ...
    def remove(
        self, _type: peer_class_type_filter_socket_type_t, _class: int
    ) -> None: ...

class peer_class_type_filter_socket_type_t(int):
    i2p_socket: peer_class_type_filter_socket_type_t
    names: Mapping[str, peer_class_type_filter_socket_type_t]
    ssl_tcp_socket: peer_class_type_filter_socket_type_t
    ssl_utp_socket: peer_class_type_filter_socket_type_t
    tcp_socket: peer_class_type_filter_socket_type_t
    utp_socket: peer_class_type_filter_socket_type_t
    values: Mapping[int, peer_class_type_filter_socket_type_t]

class peer_connect_alert(peer_alert):
    pass

class peer_disconnected_alert(peer_alert):
    error: error_code
    msg: str
    op: operation_t
    reason: close_reason_t
    socket_type: socket_type_t

class peer_error_alert(peer_alert):
    error: error_code
    op: operation_t

peer_id = sha1_hash

class peer_info:
    bw_disk: int
    bw_global: int
    bw_idle: int
    bw_limit: int
    bw_network: int
    bw_torrent: int
    choked: int
    connecting: int
    dht: int
    endgame_mode: int
    handshake: int
    holepunched: int
    http_seed: int
    interesting: int
    local_connection: int
    lsd: int
    on_parole: int
    optimistic_unchoke: int
    outgoing_connection: int
    pex: int
    plaintext_encrypted: int
    queued: int
    rc4_encrypted: int
    remote_choked: int
    remote_interested: int
    resume_data: int
    seed: int
    snubbed: int
    standard_bittorrent: int
    supports_extensions: int
    tracker: int
    upload_only: int
    web_seed: int
    client: bytes
    # connection_type: TODO
    down_speed: int
    download_limit: int
    download_queue_length: int
    download_queue_time: int
    download_rate_peak: int
    downloading_block_index: int
    downloading_piece_index: int
    downloading_progress: int
    downloading_total: int
    estimated_reciprocation_rate: int
    failcount: int
    flags: int
    ip: Tuple[str, int]
    last_active: int
    last_request: int
    load_balancing: int
    local_endpoint: Tuple[str, int]
    num_hashfails: int
    num_pieces: int
    payload_down_speed: int
    payload_up_speed: int
    pending_disk_bytes: int
    pid: sha1_hash
    pieces: List[bool]
    progress: float
    progress_ppm: int
    queue_bytes: int
    read_state: int
    receive_buffer_size: int
    recevie_quota: int
    remote_dl_rate: int
    request_timeout: int
    rtt: int
    send_buffer_size: int
    send_quota: int
    source: int
    total_download: int
    total_upload: int
    up_speed: int
    upload_limit: int
    upload_queue_length: int
    upload_rate_peak: int
    used_receive_buffer: int
    used_send_buffer: int
    write_state: int

class peer_log_alert(peer_alert):
    def log_message(self) -> str: ...
    def msg(self) -> str: ...

class peer_request:
    def __eq__(self, other: Any) -> bool: ...
    length: int
    piece: int
    start: int

class peer_snubbed_alert(peer_alert):
    pass

class peer_unsnubbed_alert(peer_alert):
    pass

class performance_alert(torrent_alert):
    warning_code: performance_warning_t

class performance_warning_t(int):
    bittyrant_with_no_uplimit: performance_warning_t
    download_limit_too_low: performance_warning_t
    names: Mapping[str, performance_warning_t]
    outstanding_disk_buffer_limit_reached: performance_warning_t
    outstanding_request_limit_reached: performance_warning_t
    send_buffer_watermark_too_low: performance_warning_t
    too_few_file_descriptors: performance_warning_t
    too_few_outgoing_ports: performance_warning_t
    too_high_disk_queue_limit: performance_warning_t
    too_many_optimistic_unchoke_slots: performance_warning_t
    upload_limit_too_low: performance_warning_t
    values: Mapping[int, performance_warning_t]

class picker_flags_t:
    backup1: int
    backup2: int
    end_game: int
    extent_affinity: int
    partial_ratio: int
    prefer_contiguous: int
    prio_sequential_pieces: int
    prioritize_partials: int
    random_pieces: int
    rarest_first: int
    rarest_first_partials: int
    reverse_pieces: int
    reverse_rarest_first: int
    reverse_sequential: int
    sequential_pieces: int
    suggested_pieces: int
    time_critical: int

class picker_log_alert(peer_alert):
    def blocks(self) -> List[int]: ...
    picker_flags: int

class piece_finished_alert(torrent_alert):
    piece_index: int

class portmap_alert(alert):
    external_port: int
    map_protocol: portmap_protocol
    map_transport: portmap_transport
    map_type: int
    mapping: int
    type: int

class portmap_error_alert(alert):
    error: error_code
    map_transport: portmap_transport
    map_type: int
    mapping: int
    msg: str
    type: int

class portmap_log_alert(alert):
    map_transport: portmap_transport
    map_type: int
    msg: str
    type: int

class portmap_protocol(int):
    names: Mapping[str, portmap_protocol]
    none: portmap_protocol
    tcp: portmap_protocol
    udp: portmap_protocol
    values: Mapping[int, portmap_protocol]

class portmap_transport(int):
    names: Mapping[str, portmap_transport]
    natpmp: portmap_transport
    upnp: portmap_transport
    values: Mapping[int, portmap_transport]

class protocol_type:
    tcp: portmap_protocol
    udp: portmap_protocol

class protocol_version(int):
    V1: protocol_version
    V2: protocol_version
    names: Mapping[str, protocol_version]
    values: Mapping[int, protocol_version]

class proxy_type_t(int):
    http: proxy_type_t
    http_pw: proxy_type_t
    i2p_proxy: proxy_type_t
    names: Mapping[str, proxy_type_t]
    none: proxy_type_t

    class proxy_settings:
        hostname: str
        port: int
        password: str
        username: str
        type: proxy_type_t
        proxy_peer_connections: bool
        proxy_hostnames: bool
    proxy_type: proxy_type_t
    socks4: proxy_type_t
    socks5: proxy_type_t
    socks5_pw: proxy_type_t
    values: Mapping[int, proxy_type_t]

class read_piece_alert(torrent_alert):
    buffer: bytes
    ec: error_code
    error: error_code
    piece: int
    size: int

class reannounce_flags_t:
    ignore_min_interval: int

class reason_t(int):
    i2p_mixed: reason_t
    invalid_local_interface: reason_t
    ip_filter: reason_t
    names: Mapping[str, reason_t]
    port_filter: reason_t
    privileged_ports: reason_t
    tcp_disabled: reason_t
    utp_disabled: reason_t
    values: Mapping[int, reason_t]

class request_dropped_alert(peer_alert):
    block_index: int
    piece_index: int

class save_resume_data_alert(torrent_alert):
    params: add_torrent_params
    resume_data: bytes

class save_resume_data_failed_alert(torrent_alert):
    error: error_code
    msg: str

class save_resume_flags_t:
    flush_disk_cache: int
    only_if_modified: int
    save_info_dict: int

class save_state_flags_t:
    all: int
    save_as_map: int
    save_dht_proxy: int
    save_dht_settings: int
    save_dht_state: int
    save_encryption_settings: int
    save_i2p_proxy: int
    save_peer_proxy: int
    save_proxy: int
    save_settings: int
    save_tracker_proxy: int
    save_web_proxy: int

class scrape_failed_alert(tracker_alert):
    def error_message(self) -> str: ...
    error: error_code
    msg: str

class scrape_reply_alert(tracker_alert):
    complete: int
    incomplete: int

class seed_choking_algorithm_t(int):
    anti_leech: seed_choking_algorithm_t
    fastest_upload: seed_choking_algorithm_t
    names: Mapping[str, seed_choking_algorithm_t]
    round_robin: seed_choking_algorithm_t
    values: Mapping[int, seed_choking_algorithm_t]

class _PeerClassInfo(TypedDict):
    ignore_unchoke_slots: bool
    connection_limit_factor: int
    label: str
    upload_limit: int
    download_limit: int
    upload_priority: int
    download_priority: int

class session:
    delete_files: int
    delete_partfile: int
    global_peer_class_id: int
    local_peer_class_id: int
    reopen_map_ports: int
    tcp: portmap_protocol
    tcp_peer_class_id: int
    udp: portmap_protocol
    @overload
    def __init__(
        self, fingerprint: fingerprint = ..., flags: int = ..., alert_mask: int = ...
    ) -> None: ...
    @overload
    def __init__(self, settings: Dict[str, Any], flags: int = ...) -> None: ...
    def add_dht_node(self, _endpoint: Tuple[str, int]) -> None: ...
    def add_dht_router(self, router: str, port: int) -> None: ...
    def add_extension(self, _extension: Any) -> None: ...
    def add_port_mapping(
        self, _proto: portmap_protocol, _int: int, _ext: int
    ) -> List[int]: ...
    @overload
    def add_torrent(
        self,
        ti: torrent_info,
        save: _PathLike,
        resume_data: Optional[_Entry] = ...,
        storage_mode: storage_mode_t = ...,
        paused: bool = ...,
    ) -> torrent_handle: ...
    @overload
    def add_torrent(self, _atp: add_torrent_params) -> torrent_handle: ...
    @overload
    def add_torrent(
        self, _bdecoded: Union[Dict[str, Any], _AddTorrentParamsDict]
    ) -> torrent_handle: ...
    def apply_settings(self, _settings: Dict[str, Any]) -> None: ...
    @overload
    def async_add_torrent(self, _atp: add_torrent_params) -> None: ...
    @overload
    def async_add_torrent(self, _bdecoded: Dict[str, Any]) -> None: ...
    def create_peer_class(self, _name: str) -> int: ...
    def delete_peer_class(self, _class: int) -> None: ...
    def delete_port_mapping(self, _mapping: int) -> None: ...
    def dht_announce(
        self, info_hash: sha1_hash, port: int = ..., flags: int = ...
    ) -> None: ...
    def dht_get_immutable_item(self, _target: sha1_hash) -> None: ...
    def dht_get_mutable_item(self, _key: bytes, _salt: bytes) -> None: ...
    def dht_get_peers(self, _info_hash: sha1_hash) -> None: ...
    def dht_live_nodes(self, _info_hash: sha1_hash) -> None: ...
    def dht_proxy(self) -> proxy_type_t.proxy_settings: ...
    def dht_put_immutable_item(self, _entry: _Entry) -> sha1_hash: ...
    def dht_put_mutable_item(
        self, _private: bytes, _public: bytes, _data: bytes, _salt: bytes
    ) -> None: ...
    def dht_sample_infohashes(
        self, _endpoint: Tuple[str, int], _target: sha1_hash
    ) -> None: ...
    def dht_state(self) -> Dict[bytes, Any]: ...
    def download_rate_limit(self) -> int: ...
    def find_torrent(self, _info_hash: sha1_hash) -> torrent_handle: ...
    def get_dht_settings(self) -> dht_settings: ...
    def get_ip_filter(self) -> ip_filter: ...
    def get_pe_settings(self) -> pe_settings: ...
    def get_peer_class(self, _class: int) -> _PeerClassInfo: ...
    def get_settings(self) -> Dict[str, Any]: ...
    def get_torrent_status(
        self, pred: Callable[[torrent_status], bool], flags: int = ...
    ) -> List[torrent_status]: ...
    def get_torrents(self) -> List[torrent_handle]: ...
    def i2p_proxy(self) -> proxy_type_t.proxy_settings: ...
    def id(self) -> sha1_hash: ...
    def is_dht_running(self) -> bool: ...
    def is_listening(self) -> bool: ...
    def is_paused(self) -> bool: ...
    def listen_on(
        self, min: int, max: int, interface: Optional[str] = ..., flags: int = ...
    ) -> None: ...
    def listen_port(self) -> int: ...
    def load_state(self, entry: _Entry, flags: int = ...) -> None: ...
    def local_download_rate_limit(self) -> int: ...
    def local_upload_rate_limit(self) -> int: ...
    def max_connections(self) -> int: ...
    def num_connections(self) -> int: ...
    def outgoing_ports(self, _min: int, _max: int) -> None: ...
    def pause(self) -> None: ...
    def peer_proxy(self) -> proxy_type_t.proxy_settings: ...
    def pop_alerts(self) -> List[alert]: ...
    def post_dht_stats(self) -> None: ...
    def post_session_stats(self) -> None: ...
    def post_torrent_updates(self, flags: int = ...) -> None: ...
    def proxy(self) -> proxy_type_t.proxy_settings: ...
    def refresh_torrent_status(
        self, torrents: List[torrent_status], flags: int = ...
    ) -> List[torrent_status]: ...
    def remove_torrent(self, _handle: torrent_handle, option: int = ...) -> None: ...
    def reopen_network_sockets(self, _options: int) -> None: ...
    def resume(self) -> None: ...
    def save_state(self, flags: int = ...) -> Dict[bytes, Any]: ...
    def session_state(self, flags: int = ...) -> session_params: ...
    def set_alert_fd(self, _fd: int) -> None: ...
    def set_alert_mask(self, _mask: int) -> None: ...
    def set_alert_notify(self, _callback: Callable[[], Any]) -> None: ...
    def set_alert_queue_size_limit(self, _limit: int) -> int: ...
    def set_dht_proxy(self, _settings: proxy_type_t.proxy_settings) -> None: ...
    def set_dht_settings(self, _settings: dht_settings) -> None: ...
    def set_download_rate_limit(self, _rate: int) -> None: ...
    def set_i2p_proxy(self, _settings: proxy_type_t.proxy_settings) -> None: ...
    def set_ip_filter(self, _filter: ip_filter) -> None: ...
    def set_local_download_rate_limit(self, _rate: int) -> None: ...
    def set_local_upload_rate_limit(self, _rate: int) -> None: ...
    def set_max_connections(self, _num: int) -> None: ...
    def set_max_half_open_connections(self, _num: int) -> None: ...
    def set_max_uploads(self, _num: int) -> None: ...
    def set_pe_settings(self, _settings: pe_settings) -> None: ...
    def set_peer_class(
        self, _class: int, _info: Union[Dict[str, Any], _PeerClassInfo]
    ) -> None: ...
    def set_peer_class_filter(self, _filter: ip_filter) -> None: ...
    def set_peer_class_type_filter(self, _pctf: peer_class_type_filter) -> None: ...
    def set_peer_id(self, _pid: sha1_hash) -> None: ...
    def set_peer_proxy(self, _settings: proxy_type_t.proxy_settings) -> None: ...
    def set_proxy(self, _settings: proxy_type_t.proxy_settings) -> None: ...
    def set_tracker_proxy(self, _settings: proxy_type_t.proxy_settings) -> None: ...
    def set_upload_rate_limit(self, _rate: int) -> None: ...
    def set_web_seed_proxy(self, _settings: proxy_type_t.proxy_settings) -> None: ...
    def start_dht(self) -> None: ...
    def start_lsd(self) -> None: ...
    def start_natpmp(self) -> None: ...
    def start_upnp(self) -> None: ...
    def status(self) -> session_status: ...
    def stop_dht(self) -> None: ...
    def stop_lsd(self) -> None: ...
    def stop_natpmp(self) -> None: ...
    def stop_upnp(self) -> None: ...
    def tracker_proxy(self) -> proxy_type_t.proxy_settings: ...
    def upload_rate_limit(self) -> int: ...
    def wait_for_alert(self, _ms: int) -> Optional[alert]: ...
    def web_seed_proxy(self) -> proxy_type_t.proxy_settings: ...

class session_flags_t:
    add_default_plugins: int
    paused: int
    start_default_features: int

class session_params:
    @overload
    def __init__(self) -> None: ...
    @overload
    def __init__(self, _settings: Dict[str, Any]) -> None: ...
    dht_state: dht_state
    ip_filter: ip_filter
    settings: Dict[str, Any]

class session_stats_alert(alert):
    values: Dict[str, int]

class session_stats_header_alert(alert):
    pass

class _UtpStatsDict(TypedDict):
    num_idle: int
    num_syn_sent: int
    num_connected: int
    num_fin_sent: int
    num_close_wait: int

class session_status:
    active_requests: List[dht_lookup]
    allowed_upload_slots: int
    dht_download_rate: int
    dht_global_nodes: int
    dht_node_cache: int
    dht_nodes: int
    dht_torrents: int
    dht_total_allocations: int
    dht_upload_rate: int
    down_bandwidth_bytes_queue: int
    down_bandwidth_queue: int
    download_rate: int
    has_incoming_connections: bool
    ip_overhead_download_rate: int
    ip_overhead_upload_rate: int
    num_peers: int
    num_unchoked: int
    optimistic_unchoke_counter: int
    payload_download_rate: int
    payload_upload_rate: int
    total_dht_download: int
    total_dht_upload: int
    total_download: int
    total_failed_bytes: int
    total_ip_overhead_download: int
    total_ip_overhead_upload: int
    total_payload_download: int
    total_payload_upload: int
    total_redundant_bytes: int
    total_tracker_download: int
    total_tracker_upload: int
    total_upload: int
    tracker_download_rate: int
    tracker_upload_rate: int
    unchoke_counter: int
    up_bandwidth_bytes_queue: int
    up_bandwidth_queue: int
    upload_rate: int
    utp_stats: _UtpStatsDict

class sha1_hash:
    @overload
    def __init__(self) -> None: ...
    @overload
    def __init__(self, _digest: bytes) -> None: ...
    def clear(self) -> None: ...
    def is_all_zeros(self) -> bool: ...
    def to_bytes(self) -> bytes: ...
    def to_string(self) -> bytes: ...
    def __eq__(self, other: Any) -> bool: ...
    def __hash__(self) -> int: ...
    def __lt__(self, other: Any) -> bool: ...
    def __ne__(self, other: Any) -> bool: ...

class sha256_hash:
    @overload
    def __init__(self) -> None: ...
    @overload
    def __init__(self, _digest: bytes) -> None: ...
    def clear(self) -> None: ...
    def is_all_zeros(self) -> bool: ...
    def to_bytes(self) -> bytes: ...
    def to_string(self) -> bytes: ...
    def __eq__(self, other: Any) -> bool: ...
    def __hash__(self) -> int: ...
    def __lt__(self, other: Any) -> bool: ...
    def __ne__(self, other: Any) -> bool: ...

class socket_type_t(int):
    http: socket_type_t
    http_ssl: socket_type_t
    i2p: socket_type_t
    names: Mapping[str, socket_type_t]
    socks5: socket_type_t
    socks5_ssl: socket_type_t
    tcp: socket_type_t
    tcp_ssl: socket_type_t
    udp: socket_type_t
    utp: socket_type_t
    utp_ssl: socket_type_t
    values: Mapping[int, socket_type_t]

class socks5_alert(alert):
    error: error_code
    ip: Tuple[str, int]
    op: operation_t

class state_changed_alert(torrent_alert):
    prev_state: torrent_status.states
    state: torrent_status.states

class state_update_alert(alert):
    status: List[torrent_status]

class stats_alert(torrent_alert):
    interval: int
    transferred: List[int]

class stats_channel(int):
    download_dht_protocol: stats_channel
    download_ip_protocol: stats_channel
    download_payload: stats_channel
    download_protocol: stats_channel
    download_tracker_protocol: stats_channel
    names: Mapping[str, stats_channel]
    upload_dht_protocol: stats_channel
    upload_ip_protocol: stats_channel
    upload_payload: stats_channel
    upload_protocol: stats_channel
    upload_tracker_protocol: stats_channel
    values: Mapping[int, stats_channel]

class stats_metric:
    name: str
    type: metric_type_t
    value_index: int

class status_flags_t:
    query_accurate_download_counters: int
    query_distributed_copies: int
    query_last_seen_complete: int
    query_pieces: int
    query_verified_pieces: int

class storage_mode_t(int):
    names: Mapping[str, storage_mode_t]
    storage_mode_allocate: storage_mode_t
    storage_mode_sparse: storage_mode_t
    values: Mapping[int, storage_mode_t]

class storage_moved_alert(torrent_alert):
    def old_path(self) -> str: ...
    def storage_path(self) -> str: ...
    path: str

class storage_moved_failed_alert(torrent_alert):
    def file_path(self) -> str: ...
    error: error_code
    op: operation_t
    operation: str

class suggest_mode_t(int):
    names: Mapping[str, suggest_mode_t]
    no_piece_suggestions: suggest_mode_t
    suggest_read_cache: suggest_mode_t
    values: Mapping[int, suggest_mode_t]

class torrent_added_alert(torrent_alert):
    pass

class torrent_alert(alert):
    handle: torrent_handle
    torrent_name: str

class torrent_checked_alert(torrent_alert):
    pass

class torrent_delete_failed_alert(torrent_alert):
    error: error_code
    info_hash: sha1_hash
    info_hashes: info_hash_t
    msg: str

class torrent_deleted_alert(torrent_alert):
    info_hash: sha1_hash
    info_hashes: info_hash_t

class torrent_error_alert(torrent_alert):
    error: error_code

class torrent_finished_alert(torrent_alert):
    pass

class torrent_flags:
    apply_ip_filter: int
    auto_managed: int
    default_dont_download: int
    default_flags: int
    disable_dht: int
    disable_lsd: int
    disable_pex: int
    duplicate_is_error: int
    no_verify_files: int
    override_trackers: int
    override_web_seeds: int
    paused: int
    seed_mode: int
    sequential_download: int
    share_mode: int
    stop_when_ready: int
    super_seeding: int
    update_subscribe: int
    upload_mode: int

class _BlockInfoDict(TypedDict):
    state: int
    num_peers: int
    bytes_progress: int
    block_size: int
    peer: Tuple[str, int]

class _PartialPieceInfoDict(TypedDict):
    piece_index: int
    blocks_in_piece: int
    blocks: List[_BlockInfoDict]

class _ErrorCodeDict(TypedDict):
    value: int
    category: str

class _AnnounceInfohashDict(TypedDict):
    message: str
    last_error: _ErrorCodeDict
    next_announce: Optional[datetime.datetime]
    min_announce: Optional[datetime.datetime]
    scrape_incomplete: int
    scrape_complete: int
    scrape_downloaded: int
    fails: int
    updating: bool
    start_sent: bool
    complete_sent: bool

class _AnnounceEndpointDict(TypedDict):
    local_address: Tuple[str, int]
    info_hashes: List[_AnnounceInfohashDict]
    message: str
    last_error: _ErrorCodeDict
    next_announce: Optional[datetime.datetime]
    min_announce: Optional[datetime.datetime]
    scrape_incomplete: int
    scrape_complete: int
    scrape_downloaded: int
    fails: int
    updating: bool
    start_sent: bool
    complete_sent: bool

class _AnnounceEntryDict(TypedDict):
    url: str
    trackerid: str
    tier: int
    fail_limit: int
    source: int
    verified: bool
    message: str
    last_error: _ErrorCodeDict
    next_announce: Optional[datetime.datetime]
    min_announce: Optional[datetime.datetime]
    scrape_incomplete: int
    scrape_complete: int
    scrape_downloaded: int
    fails: int
    updating: bool
    start_sent: bool
    complete_sent: bool
    endpoints: List[_AnnounceEndpointDict]
    send_stats: bool

class torrent_handle:
    alert_when_available: int
    flush_disk_cache: int
    graceful_pause: int
    ignore_min_interval: int
    only_if_modified: int
    overwrite_existing: int
    piece_granularity: int
    query_accurate_download_counters: int
    query_distributed_copies: int
    query_last_seen_complete: int
    query_pieces: int
    query_verified_pieces: int
    save_info_dict: int
    def add_http_seed(self, _url: str) -> None: ...
    def add_piece(self, _index: int, _data: bytes, _flags: int) -> None: ...
    def add_tracker(self, _announce: Dict[str, Any]) -> None: ...
    def add_url_seed(self, _url: str) -> None: ...
    def apply_ip_filter(self, _enabled: bool) -> None: ...
    def auto_managed(self, _enabled: bool) -> None: ...
    def clear_error(self) -> None: ...
    def clear_piece_deadlines(self) -> None: ...
    def connect_peer(
        self, _endpoint: Tuple[str, int], source: int = ..., flags: int = ...
    ) -> None: ...
    def download_limit(self) -> int: ...
    def file_priorities(self) -> List[int]: ...
    @overload
    def file_priority(self, _prio: int) -> int: ...
    @overload
    def file_priority(self, _index: int, _prio: int) -> None: ...
    def file_progress(self, flags: int = ...) -> List[int]: ...
    def file_status(self) -> List[open_file_state]: ...
    def flags(self) -> int: ...
    def flush_cache(self) -> None: ...
    def force_dht_announce(self) -> None: ...
    def force_reannounce(
        self, seconds: int = ..., tracker_idx: int = ..., flags: int = ...
    ) -> None: ...
    def force_recheck(self) -> None: ...
    def get_download_queue(self) -> List[_PartialPieceInfoDict]: ...
    def get_file_priorities(self) -> List[int]: ...
    def get_peer_info(self) -> List[peer_info]: ...
    def get_piece_priorities(self) -> List[int]: ...
    def get_torrent_info(self) -> torrent_info: ...
    def has_metadata(self) -> bool: ...
    def have_piece(self, _index: int) -> bool: ...
    def http_seeds(self) -> List[str]: ...
    def info_hash(self) -> sha1_hash: ...
    def info_hashes(self) -> info_hash_t: ...
    def is_auto_managed(self) -> bool: ...
    def is_finished(self) -> bool: ...
    def is_paused(self) -> bool: ...
    def is_seed(self) -> bool: ...
    def is_valid(self) -> bool: ...
    def max_connections(self) -> int: ...
    def max_uploads(self) -> int: ...
    def move_storage(self, path: _PathLike, flags: int = ...) -> None: ...
    def name(self) -> str: ...
    def need_save_resume_data(self) -> bool: ...
    def pause(self, flags: int = ...) -> None: ...
    def piece_availability(self) -> List[int]: ...
    def piece_priorities(self) -> List[int]: ...
    @overload
    def piece_priority(self, _index: int) -> int: ...
    @overload
    def piece_priority(self, _index: int, _prio: int) -> None: ...
    def prioritize_files(self, _priorities: List[int]) -> None: ...
    @overload
    def prioritize_pieces(self, _priorities: List[Tuple[int, int]]) -> None: ...
    @overload
    def prioritize_pieces(self, __priorities: List[int]) -> None: ...
    def queue_position(self) -> int: ...
    def queue_position_bottom(self) -> None: ...
    def queue_position_down(self) -> None: ...
    def queue_position_top(self) -> None: ...
    def queue_position_up(self) -> None: ...
    def read_piece(self, _index: int) -> None: ...
    def remove_http_seed(self, _url: str) -> None: ...
    def remove_url_seed(self, _url: str) -> None: ...
    def rename_file(self, _index: int, _path: str) -> None: ...
    def replace_trackers(
        self, _entries: Iterable[Union[announce_entry, Dict[str, Any]]]
    ) -> None: ...
    def reset_piece_deadline(self, _index: int) -> None: ...
    def resume(self) -> None: ...
    def save_path(self) -> str: ...
    def save_resume_data(self, flags: int = ...) -> None: ...
    def scrape_tracker(self, index: int = ...) -> None: ...
    def set_download_limit(self, _limit: int) -> None: ...
    @overload
    def set_flags(self, _flags: int) -> None: ...
    @overload
    def set_flags(self, _flags: int, _mask: int) -> None: ...
    def set_max_connections(self, _num: int) -> None: ...
    def set_max_uploads(self, _num: int) -> None: ...
    def set_metadata(self, _metadata: bytes) -> None: ...
    def set_peer_download_limit(
        self, _endpoint: Tuple[str, int], _limit: int
    ) -> None: ...
    def set_peer_upload_limit(
        self, _endpoint: Tuple[str, int], _limit: int
    ) -> None: ...
    def set_piece_deadline(
        self, index: int, deadline: int, flags: int = ...
    ) -> None: ...
    def set_priority(self, _prio: int) -> None: ...
    def set_ratio(self, _ratio: float) -> None: ...
    def set_sequential_download(self, _enabled: bool) -> None: ...
    def set_share_mode(self, _enabled: bool) -> None: ...
    def set_ssl_certificate(
        self,
        cert: _PathLike,
        private_key: _PathLike,
        dh_params: _PathLike,
        passphrase: str = ...,
    ) -> None: ...
    def set_tracker_login(self, _user: str, _pass: str) -> None: ...
    def set_upload_limit(self, _limit: int) -> None: ...
    def set_upload_mode(self, _enabled: bool) -> None: ...
    def status(self, flags: int = ...) -> torrent_status: ...
    def stop_when_ready(self, _enabled: bool) -> None: ...
    @overload
    def super_seeding(self, _enabled: bool) -> None: ...
    @overload
    def super_seeding(self) -> bool: ...
    def torrent_file(self) -> Optional[torrent_info]: ...
    def trackers(self) -> List[_AnnounceEntryDict]: ...
    def unset_flags(self, _flags: int) -> None: ...
    def upload_limit(self) -> int: ...
    def url_seeds(self) -> List[str]: ...
    def use_interface(self, _name: str) -> None: ...
    def write_resume_data(self) -> Dict[bytes, Any]: ...
    def __eq__(self, other: Any) -> bool: ...
    def __hash__(self) -> int: ...
    def __lt__(self, other: Any) -> bool: ...
    def __ne__(self, other: Any) -> bool: ...

class _WebSeedDict(TypedDict):
    url: str
    auth: str

class torrent_info:
    @overload
    def __init__(self, _info_hashes: info_hash_t) -> None: ...
    @overload
    def __init__(self, _info_hash: sha1_hash) -> None: ...
    @overload
    def __init__(self, _info_hash: sha256_hash) -> None: ...
    @overload
    def __init__(self, ti: torrent_info) -> None: ...
    @overload
    def __init__(self, _path: str) -> None: ...
    @overload
    def __init__(self, _path: str, _limits: Dict[str, Any]) -> None: ...
    @overload
    def __init__(self, _bencoded: bytes) -> None: ...
    @overload
    def __init__(self, _bencoded: bytes, _limits: Dict[str, Any]) -> None: ...
    @overload
    def __init__(self, _bdecoded: Dict[bytes, Any]) -> None: ...
    @overload
    def __init__(
        self, _bdecoded: Dict[bytes, Any], _limits: Dict[str, Any]
    ) -> None: ...
    def add_http_seed(
        self,
        url: str,
        extern_auth: str = ...,
        extra_headers: List[Tuple[str, str]] = ...,
    ) -> None: ...
    def add_node(self, _ip: str, _port: int) -> None: ...
    def add_tracker(
        self, url: str, tier: int = ..., source: tracker_source = ...
    ) -> None: ...
    def add_url_seed(
        self,
        url: str,
        extern_auth: str = ...,
        extra_headers: List[Tuple[str, str]] = ...,
    ) -> None: ...
    def collections(self) -> List[str]: ...
    def comment(self) -> str: ...
    def creation_date(self) -> int: ...
    def creator(self) -> str: ...
    def file_at(self, _index: int) -> file_entry: ...
    def files(self) -> file_storage: ...
    def hash_for_piece(self, _index: int) -> bytes: ...
    def info_hash(self) -> sha1_hash: ...
    def info_hashes(self) -> info_hash_t: ...
    def info_section(self) -> bytes: ...
    def is_i2p(self) -> bool: ...
    def is_merkle_torrent(self) -> bool: ...
    def is_valid(self) -> bool: ...
    def map_block(self, _piece: int, _offset: int, _size: int) -> List[file_slice]: ...
    def map_file(self, _file: int, _offset: int, _size: int) -> peer_request: ...
    def merkle_tree(self) -> List[bytes]: ...
    def metadata(self) -> bytes: ...
    def metadata_size(self) -> int: ...
    def name(self) -> str: ...
    def nodes(self) -> List[Tuple[str, int]]: ...
    def num_files(self) -> int: ...
    def num_pieces(self) -> int: ...
    def orig_files(self) -> file_storage: ...
    def piece_length(self) -> int: ...
    def piece_size(self, _index: int) -> int: ...
    def priv(self) -> bool: ...
    def remap_files(self, _fs: file_storage) -> None: ...
    def rename_file(self, _index: int, _path: str) -> None: ...
    def set_merkle_tree(self, _tree: List[bytes]) -> None: ...
    def set_web_seeds(self, _seeds: List[Dict[str, Any]]) -> None: ...
    def similar_torrents(self) -> List[sha1_hash]: ...
    def size_on_disk(self) -> int: ...
    def ssl_cert(self) -> str: ...
    def total_size(self) -> int: ...
    def trackers(self) -> Iterator[announce_entry]: ...
    def web_seeds(self) -> List[_WebSeedDict]: ...

class torrent_log_alert(torrent_alert):
    def log_message(self) -> str: ...
    def msg(self) -> str: ...

class torrent_need_cert_alert(torrent_alert):
    error: error_code

class torrent_paused_alert(torrent_alert):
    pass

class torrent_removed_alert(torrent_alert):
    info_hash: sha1_hash
    info_hashes: info_hash_t

class torrent_resumed_alert(torrent_alert):
    pass

class torrent_status:
    allocating: torrent_status.states
    checking_files: torrent_status.states
    checking_resume_data: torrent_status.states
    downloading: torrent_status.states
    downloading_metadata: torrent_status.states
    finished: torrent_status.states
    queued_for_checking: torrent_status.states
    seeding: torrent_status.states

    class states(int):
        allocating: torrent_status.states
        checking_files: torrent_status.states
        checking_resume_data: torrent_status.states
        downloading: torrent_status.states
        downloading_metadata: torrent_status.states
        finished: torrent_status.states
        names: Mapping[str, torrent_status.states]
        queued_for_checking: torrent_status.states
        seeding: torrent_status.states
        values: Mapping[int, torrent_status.states]
    def __eq__(self, other: Any) -> bool: ...
    active_duration: datetime.timedelta
    active_time: int
    added_time: int
    all_time_download: int
    all_time_upload: int
    announce_interval: datetime.timedelta
    announcing_to_dht: bool
    announcing_to_lsd: bool
    announcing_to_trackers: bool
    auto_managed: bool
    block_size: int
    completed_time: int
    connect_candidates: int
    connections_limit: int
    current_tracker: str
    distributed_copies: float
    distributed_fraction: int
    distributed_full_copies: int
    down_bandwidth_queue: int
    download_payload_rate: int
    download_rate: int
    errc: error_code
    error: str
    error_file: int
    finished_duration: datetime.timedelta
    finished_time: int
    flags: int
    handle: torrent_handle
    has_incoming: bool
    has_metadata: bool
    info_hash: sha1_hash
    info_hashes: info_hash_t
    ip_filter_applies: bool
    is_finished: bool
    is_loaded: bool
    is_seeding: bool
    last_download: Optional[datetime.datetime]
    last_scrape: int
    last_seen_complete: int
    last_upload: Optional[datetime.datetime]
    list_peers: int
    list_seeds: int
    moving_storage: bool
    name: str
    need_save_resume: bool
    next_announce: datetime.timedelta
    num_complete: int
    num_connections: int
    num_incomplete: int
    num_peers: int
    num_pieces: int
    num_seeds: int
    num_uploads: int
    paused: bool
    pieces: List[bool]
    priority: int
    progress: float
    progress_ppm: int
    queue_position: int
    save_path: str
    seed_mode: bool
    seed_rank: int
    seeding_duration: datetime.timedelta
    seeding_time: int
    sequential_download: bool
    share_mode: bool
    state: torrent_status.states
    stop_when_ready: bool
    storage_mode: storage_mode_t
    super_seeding: bool
    time_since_download: int
    time_since_upload: int
    torrent_file: Optional[torrent_info]
    total_done: int
    total_download: int
    total_failed_bytes: int
    total_payload_download: int
    total_payload_upload: int
    total_redundant_bytes: int
    total_upload: int
    total_wanted: int
    total_wanted_done: int
    up_bandwidth_queue: int
    upload_mode: bool
    upload_payload_rate: int
    upload_rate: int
    uploads_limit: int
    verified_pieces: List[bool]

class tracker_alert(torrent_alert):
    def tracker_url(self) -> str: ...
    local_endpoint: Tuple[str, int]
    url: str

class tracker_announce_alert(tracker_alert):
    event: event_t

class tracker_error_alert(tracker_alert):
    def error_message(self) -> str: ...
    def failure_reason(self) -> str: ...
    error: error_code
    msg: str
    status_code: int
    times_in_row: int

class tracker_reply_alert(tracker_alert):
    num_peers: int

class tracker_source(int):
    names: Mapping[str, tracker_source]
    source_client: tracker_source
    source_magnet_link: tracker_source
    source_tex: tracker_source
    source_torrent: tracker_source
    values: Mapping[int, tracker_source]

class tracker_warning_alert(tracker_alert):
    pass

class udp_error_alert(alert):
    endpoint: Tuple[str, int]
    error: error_code

class unwanted_block_alert(peer_alert):
    block_index: int
    piece_index: int

class url_seed_alert(torrent_alert):
    def error_message(self) -> str: ...
    def server_url(self) -> str: ...
    error: error_code
    msg: str
    url: str
