"""
I created this file by starting with stubgen and modifying its output.

I recommend that maintainers occasionally re-run stubgen, and compare it to
this file. This diff will be too verbose to use for small changes, but it's the
only way to ensure coverage.

To help with the above process, please keep everything in sorted order, to
match stubgen's output.

Typing notes:
- This project has a very particular approach to typed enum values. Pay
  special attention to which are typed, and which are just ints.
- Some attributes like add_torrent_params.save_path ought to be read as one
  type (str) and set as another (str | bytes), but mypy doesn't support
  this.
- We use strict primitive types (dict and list) for function args, because
  boost strictly requires these, and will not understand duck-typed
  abstractions like Mapping or Sequence.
- I chose to make the "names" and "values" enum class attributes typed as
  Mapping, specifically to block modification. I coludn't think of a case
  where these would need to be used in a dict context.
- Boost mangles function signatures to all look like (*args, **kwargs) in
  python. This allows for "anonymous" positional args which python doesn't
  normally support. We want the type stubs to match the *usage* of a function,
  not this mangled version, so for anonymous positional args, we use a name
  like "_name" (these names might fairly all be "_", but python doesn't allow
  duplicates). PEP 570 provides positional-only argument syntax but mypy
  doesn't support it as of writing.
"""
import datetime
from collections.abc import Callable, Iterator
from typing import Final, Literal, NewType, TypedDict, overload, type_check_only
from typing_extensions import TypeAlias, NotRequired

_PathLike: TypeAlias = str | bytes
_Entry: TypeAlias = bytes | str | int | list | dict[bytes, "_Entry"] | dict[str, "_Entry"] | tuple[int, ...]  # noqa: Y020
_BoostBaseClass = NewType('Boost.Python.class', object)  # type: ignore

create_smart_ban_plugin: str
create_ut_metadata_plugin: str
create_ut_pex_plugin: str
api_version: int
version: str
version_major: int
version_minor: int
__version__: str
@overload
def add_files(fs: file_storage, path: str, flags: int = 0) -> None: ...
@overload
def add_files(fs: file_storage, path: str, predicate: Callable[[str], bool], flags: int = 0) -> None: ...
def add_magnet_uri(_session: session, _uri: str, _limits: load_torrent_limits) -> torrent_handle: ...
def bdecode(_bencoded: bytes) -> _Entry: ...
def bdecode_category() -> error_category: ...
def bencode(_bdecoded: _Entry) -> bytes: ...
def client_fingerprint(_pid: sha1_hash) -> fingerprint | None: ...
def default_settings() -> settings_pack: ...
def find_metric_idx(_metric_name: str) -> int: ...
def generate_fingerprint(_prefix: str | bytes, _major: int, _minor: int, _rev: int, _tag: int) -> str: ...
def generic_category() -> error_category: ...
def get_bdecode_category() -> error_category: ...
def get_http_category() -> error_category: ...
def get_i2p_category() -> error_category: ...
def get_socks_category() -> error_category: ...
def get_upnp_category() -> error_category: ...
def high_performance_seed() -> settings_pack: ...
def http_category() -> error_category: ...
def i2p_category() -> error_category: ...
def identify_client(_pid: sha1_hash) -> str: ...
def get_libtorrent_category() -> error_category: ...
def libtorrent_category() -> error_category: ...
@overload
def load_torrent_buffer(arg1: bytes) -> add_torrent_params: ...
@overload
def load_torrent_buffer(arg1: bytes, arg2: AddTorrentParamsdict | None = None) -> add_torrent_params: ...
@overload
def load_torrent_file(arg1: str) -> add_torrent_params: ...
@overload
def load_torrent_file(arg1: str, arg2: AddTorrentParamsdict) -> add_torrent_params: ...
@overload
def load_torrent_parsed(arg1: AddTorrentParamsdict) -> add_torrent_params: ...
@overload
def load_torrent_parsed(arg1: AddTorrentParamsdict, arg2: AddTorrentParamsdict) -> add_torrent_params: ...
@overload
def make_magnet_uri(_handle: torrent_handle) -> str: ...
@overload
def make_magnet_uri(_ti: torrent_info) -> str: ...
def min_memory_usage() -> settings_pack: ...
def operation_name(_op: int) -> str: ...
def parse_magnet_uri(_uri: str) -> add_torrent_params: ...
def write_session_params_buf(buffer: session_params, flags: int = 4294967295) -> bytes: ...
def write_torrent_file(atp: add_torrent_params, flags: int = 0) -> AddTorrentParamsdict: ...
def write_torrent_file_buf(atp: add_torrent_params, flags: int = 0) -> bytes: ...


# Note that we include every param supported by add_torrent() here, though
# this type is only created by parse_magnet_uri_dict() which doesn't populate
# everything. This is to support users modifying its output before passing to
# add_torrent().
@type_check_only
class AddTorrentParamsdict(TypedDict, total=False):
    active_time: NotRequired[int]
    added_time: NotRequired[int]
    banned_peers: NotRequired[list[tuple[str, int]]]
    completed_time: NotRequired[int]
    dht_nodes: NotRequired[list[tuple[str, int]]]
    download_limit: NotRequired[int]
    file_priorities: NotRequired[list[int]]
    finished_time: NotRequired[int]
    flags: NotRequired[int]
    have_pieces: NotRequired[list[bool]]
    http_seeds: NotRequired[list[str]]
    info_hash: NotRequired[bytes]
    info_hashes: NotRequired[bytes]
    last_download: NotRequired[int]
    last_seen_complete: NotRequired[int]
    last_upload: NotRequired[int]
    max_connections: NotRequired[int]
    max_uploads: NotRequired[int]
    merkle_tree: NotRequired[list[bytes]]
    name: NotRequired[str]
    num_complete: NotRequired[int]
    num_downloaded: NotRequired[int]
    num_incomplete: NotRequired[int]
    peers: NotRequired[list[tuple[str, int]]]
    piece_priorities: NotRequired[list[int]]
    renamed_files: NotRequired[dict[int, str]]
    resume_data: NotRequired[list[str]]
    save_path: NotRequired[str]
    seeding_time: NotRequired[int]
    storage_mode: NotRequired[int]
    ti: NotRequired[torrent_info]
    total_downloaded: NotRequired[int]
    total_uploaded: NotRequired[int]
    tracker_tiers: NotRequired[list[int]]
    trackerid: NotRequired[str]
    trackers: NotRequired[list[str]]
    unfinished_pieces: NotRequired[dict[int, list[bool]]]
    upload_limit: NotRequired[int]
    url: NotRequired[str]
    url_seeds: NotRequired[list[str]]
    verified_pieces: NotRequired[list[bool]]
    version: NotRequired[int]


@type_check_only
class SessionParamsDict(dict):
    @type_check_only
    class SessionParamsDHTDict(dict):
        @overload
        def __getitem__(self, key: Literal[b"aggressive_lookups"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"block_ratelimit"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"block_timeout"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"enforce_node_id"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"extended_routing_table"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"ignore_dark_internet"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"item_lifetime"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"max_dht_items"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"max_fail_count"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"max_peers"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"max_peers_reply"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"max_torrent_search_reply"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"max_torrents"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"privacy_lookups"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"read_only"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"restrict_routing_ips"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"restrict_search_ips"]) -> int: ...
        @overload
        def __getitem__(self, key: Literal[b"search_branching"]) -> int: ...

    @overload
    def __getitem__(self, key: Literal[b"dht"]) -> SessionParamsDHTDict: ...
    @overload
    def __getitem__(self, key: Literal[b"dht state"]) -> DeprecatedDHTState: ...
    @overload
    def __getitem__(self, key: Literal[b"extensions"]) -> dict: ...
    @overload
    def __getitem__(self, key: Literal[b"ip_filter4"]) -> list[bytes]: ...
    @overload
    def __getitem__(self, key: Literal[b"ip_filter6"]) -> list[bytes]: ...
    @overload
    def __getitem__(self, key: Literal[b"settings"]) -> settings_pack: ...


@type_check_only
class ResumeDataDict(dict):
    @overload
    def __getitem__(self, key: Literal[b"active_time"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"added_time"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"allocation"]) -> bytes: ...
    @overload
    def __getitem__(self, key: Literal[b"apply_ip_filter"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"auto_managed"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"completed_time"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"disable_dht"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"disable_lsd"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"disable_pex"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"download_rate_limit"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"file-format"]) -> Literal[b"resume file"]: ...
    @overload
    def __getitem__(self, key: Literal[b"file-version"]) -> Literal[1]: ...
    @overload
    def __getitem__(self, key: Literal[b"finished_time"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"httpseeds"]) -> list: ...  # TODO
    @overload
    def __getitem__(self, key: Literal[b"i2p"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"info-hash"]) -> bytes: ...
    @overload
    def __getitem__(self, key: Literal[b"info-hash2"]) -> bytes: ...
    @overload
    def __getitem__(self, key: Literal[b"last_download"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"last_seen_complete"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"last_upload"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"version"]) -> bytes: ...
    @overload
    def __getitem__(self, key: Literal[b"max_connections"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"max_uploads"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"num_complete"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"num_downloaded"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"num_incomplete"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"paused"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"pieces"]) -> bytes: ...
    @overload
    def __getitem__(self, key: Literal[b"save_path"]) -> bytes: ...
    @overload
    def __getitem__(self, key: Literal[b"seed_mode"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"seeding_time"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"sequential_download"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"share_mode"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"stop_when_ready"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"super_seeding"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"total_downloaded"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"total_uploaded"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"trackers"]) -> list: ...  # TODO
    @overload
    def __getitem__(self, key: Literal[b"upload_mode"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"upload_rate_limit"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"url-list"]) -> list: ...  # TODO


def parse_magnet_uri_dict(_uri: str) -> AddTorrentParamsdict: ...
@overload
def read_resume_data(_bencoded: bytes) -> add_torrent_params: ...
@overload
def read_resume_data(_bencoded: bytes, _limits: load_torrent_limits) -> add_torrent_params: ...
@overload
def read_session_params(buffer: bytes, flags: int = ...) -> session_params: ...
@overload
def read_session_params(dict: SessionParamsDict, flags: int = ...) -> session_params: ...
def session_stats_metrics() -> list[stats_metric]: ...
@overload
def set_piece_hashes(_ct: create_torrent, _path: str, _callback: Callable[[int], None]) -> None: ...
@overload
def set_piece_hashes(_ct: create_torrent, _path: str) -> None: ...
def socks_category() -> error_category: ...
def system_category() -> error_category: ...
def upnp_category() -> error_category: ...
def write_resume_data(_atp: add_torrent_params) -> ResumeDataDict: ...
def write_resume_data_buf(_atp: add_torrent_params) -> bytes: ...
def write_session_params(entry: session_params, flags: int = 4294967295) -> SessionParamsDict: ...


class add_piece_flags_t(metaclass=_BoostBaseClass):
    __instance_size__: int
    @property
    def overwrite_existing(self) -> int: ...


class alert(metaclass=_BoostBaseClass):
    __name__: str

    class category_t(metaclass=_BoostBaseClass):
        __instance_size__: int
        all_categories: Literal[4294967295]
        block_progress_notification: Literal[16777216]
        connect_notification: Literal[32]
        debug_notification: Literal[32]
        dht_log_notification: Literal[131072]
        dht_notification: Literal[1024]
        dht_operation_notification: Literal[262144]
        error_notification: Literal[1]
        file_progress_notification: Literal[2097152]
        incoming_request_notification: Literal[65536]
        ip_block_notification: Literal[256]
        peer_log_notification: Literal[32768]
        peer_notification: Literal[2]
        performance_warning: Literal[512]
        picker_log_notification: Literal[1048576]
        piece_progress_notification: Literal[4194304]
        port_mapping_log_notification: Literal[524288]
        port_mapping_notification: Literal[4]
        progress_notification: Literal[128]
        session_log_notification: Literal[8192]
        stats_notification: Literal[2048]
        status_notification: Literal[64]
        storage_notification: Literal[8]
        torrent_log_notification: Literal[16384]
        tracker_notification: Literal[16]
        upload_notification: Literal[8388608]

    def category(self) -> int: ...
    def message(self) -> str: ...
    def what(self) -> str: ...


class torrent_alert(alert):
    @property
    def handle(self) -> torrent_handle: ...
    @property
    def torrent_name(self) -> str: ...


class add_torrent_alert(torrent_alert):
    @property
    def error(self) -> error_code: ...
    @property
    def params(self) -> add_torrent_params: ...


class add_torrent_params(metaclass=_BoostBaseClass):
    __instance_size__: int
    active_time: int
    added_time: int
    banned_peers: list[tuple[str, int]]
    completed_time: int
    dht_nodes: list[tuple[str, int]]
    download_limit: int
    file_priorities: list[int]
    finished_time: int
    flags: int
    have_pieces: list[bool]
    http_seeds: list[str]
    info_hash: sha1_hash
    info_hashes: info_hash_t
    last_download: int
    last_seen_complete: int
    last_upload: int
    max_connections: int
    max_uploads: int
    merkle_tree: list[sha1_hash]
    name: str
    num_complete: int
    num_downloaded: int
    num_incomplete: int
    peers: list[tuple[str, int]]
    piece_priorities: list[int]
    renamed_files: dict[int, str]
    resume_data: list[str]
    save_path: str
    seeding_time: int
    storage_mode: int
    ti: torrent_info | None
    total_downloaded: int
    total_uploaded: int
    tracker_tiers: list[int]
    trackerid: str
    trackers: list[str]
    unfinished_pieces: dict[int, list[bool]]
    upload_limit: int
    url: str
    url_seeds: list[str]
    verified_pieces: list[bool]
    version: int


class add_torrent_params_flags_t(metaclass=_BoostBaseClass):
    __instance_size__: int
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


class alert_category(metaclass=_BoostBaseClass):
    __instance_size__: int
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
    @property
    def dropped_alerts(self) -> list[bool]: ...


class announce_entry(metaclass=_BoostBaseClass):
    __instance_size__: int
    def __init__(self, url: str) -> None: ...
    def can_announce(self, _is_seed: bool) -> bool: ...
    def is_working(self) -> bool: ...
    def min_announce_in(self) -> int: ...
    def next_announce_in(self) -> int: ...
    def reset(self) -> None: ...
    def trim(self) -> None: ...
    @property
    def complete_sent(self) -> bool: ...
    @property
    def fail_limit(self) -> int: ...
    @property
    def fails(self) -> int: ...
    @property
    def last_error(self) -> error_code: ...
    @property
    def message(self) -> str: ...
    @property
    def min_announce(self) -> datetime.datetime | None: ...
    @property
    def next_announce(self) -> datetime.datetime | None: ...
    @property
    def scrape_complete(self) -> int: ...
    @property
    def scrape_downloaded(self) -> int: ...
    @property
    def scrape_incomplete(self) -> int: ...
    @property
    def send_stats(self) -> bool: ...
    @property
    def source(self) -> int: ...
    @property
    def start_sent(self) -> bool: ...
    @property
    def tier(self) -> int: ...
    @property
    def trackerid(self) -> str: ...
    @property
    def updating(self) -> bool: ...
    @property
    def url(self) -> str: ...
    @property
    def verified(self) -> bool: ...


class announce_flags_t(int):
    __instance_size__: int
    seed: int
    implied_port: int
    ssl_torrent: int
    names: Final[dict[str, int]] = {
        "seed": announce_flags_t.seed,  # noqa: F821
        "implied_port": announce_flags_t.implied_port,  # noqa: F821
        "ssl_torrent": announce_flags_t.ssl_torrent  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        1: announce_flags_t.seed,  # noqa: F821
        2: announce_flags_t.implied_port,  # noqa: F821
        4: announce_flags_t.ssl_torrent  # noqa: F821
    }


class anonymous_mode_alert(torrent_alert):
    @property
    def kind(self) -> int: ...
    @property
    def str(self) -> str: ...


class bandwidth_mixed_algo_t(int):
    peer_proportional: int
    prefer_tcp: int

    names: Final[dict[str, int]] = {
        "prefer_tcp": bandwidth_mixed_algo_t.prefer_tcp,  # noqa: F821
        "peer_proportional": bandwidth_mixed_algo_t.peer_proportional  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: bandwidth_mixed_algo_t.prefer_tcp,  # noqa: F821
        1: bandwidth_mixed_algo_t.peer_proportional  # noqa: F821
    }


class peer_alert(torrent_alert):
    @property
    def endpoint(self) -> tuple[str, int]: ...
    @property
    def ip(self) -> tuple[str, int]: ...
    @property
    def pid(self) -> sha1_hash: ...


class block_downloading_alert(peer_alert):
    @property
    def block_index(self) -> int: ...
    @property
    def peer_speedmsg(self) -> str: ...
    @property
    def piece_index(self) -> int: ...


class block_finished_alert(peer_alert):
    @property
    def block_index(self) -> int: ...
    @property
    def piece_index(self) -> int: ...


class block_timeout_alert(peer_alert):
    @property
    def block_index(self) -> int: ...
    @property
    def piece_index(self) -> int: ...


class block_uploaded_alert(peer_alert):
    @property
    def block_index(self) -> int: ...
    @property
    def piece_index(self) -> int: ...


class cache_flushed_alert(torrent_alert):
    ...


class choking_algorithm_t(int):
    auto_expand_choker: int
    bittyrant_choker: int
    fixed_slots_choker: int
    rate_based_choker: int

    names: Final[dict[str, int]] = {
        "fixed_slots_choker": choking_algorithm_t.fixed_slots_choker,  # noqa: F821
        "auto_expand_choker": choking_algorithm_t.auto_expand_choker,  # noqa: F821
        "rate_based_choker": choking_algorithm_t.rate_based_choker,  # noqa: F821
        "bittyrant_choker": choking_algorithm_t.bittyrant_choker  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: choking_algorithm_t.fixed_slots_choker,  # noqa: F821
        2: choking_algorithm_t.rate_based_choker,  # noqa: F821
        3: choking_algorithm_t.bittyrant_choker  # noqa: F821
    }


@type_check_only
class TorrentFileFileDict(dict):
    @overload
    def __getitem__(self, key: Literal[b"length"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"path"]) -> list[bytes]: ...
    @overload
    def __getitem__(self, key: Literal[b"path.utf-8"]) -> list[bytes] | None: ...


@type_check_only
class TorrentFileInfoDict(dict):
    @overload
    def __getitem__(self, key: Literal[b"files"]) -> list[TorrentFileFileDict]: ...
    @overload
    def __getitem__(self, key: Literal[b"length"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"name"]) -> bytes: ...
    @overload
    def __getitem__(self, key: Literal[b"name.utf-8"]) -> bytes: ...
    @overload
    def __getitem__(self, key: Literal[b"piece length"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"pieces"]) -> bytes: ...


@type_check_only
class TorrentFileFileSpecV2(dict):
    @overload
    def __getitem__(self, key: Literal[b"length"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"pieces root"]) -> bytes | None: ...


@type_check_only
class TorrentFileFileV2(dict):
    def __getitem__(self, key: Literal[b""]) -> TorrentFileFileSpecV2: ...


@type_check_only
class TorrentFileDirectoryV2(dict):
    def __getitem__(self, key: bytes) -> TorrentFileDirectoryV2 | TorrentFileFileV2: ...


@type_check_only
class TorrentFileInfoDictV2(dict):
    @overload
    def __getitem__(self, key: Literal[b"file tree"]) -> TorrentFileDirectoryV2: ...
    @overload
    def __getitem__(self, key: Literal[b"meta version"]) -> int: ...
    @overload
    def __getitem__(self, key: Literal[b"name"]) -> bytes: ...
    @overload
    def __getitem__(self, key: Literal[b"piece length"]) -> int: ...


@type_check_only
class TorrentFileDict(dict):
    @overload
    def __getitem__(self, key: Literal[b"announce"]) -> bytes | None: ...
    @overload
    def __getitem__(self, key: Literal[b"announce-list"]) -> list[list[bytes]] | None: ...
    @overload
    def __getitem__(self, key: Literal[b"comment"]) -> bytes | None: ...
    @overload
    def __getitem__(self, key: Literal[b"created by"]) -> bytes | None: ...
    @overload
    def __getitem__(self, key: Literal[b"creation date"]) -> bytes | None: ...
    @overload
    def __getitem__(self, key: Literal[b"encoding"]) -> bytes | None: ...
    @overload
    def __getitem__(self, key: Literal[b"info"]) -> TorrentFileInfoDict | TorrentFileInfoDictV2: ...
    @overload
    def __getitem__(self, key: Literal[b"httpseeds"]) -> list[bytes] | None: ...
    @overload
    def __getitem__(self, key: Literal[b"nodes"]) -> list[bytes] | None: ...
    @overload
    def __getitem__(self, key: Literal[b"piece layers"]) -> dict[bytes, bytes] | None: ...  # b"piece layers" are for v2 torrents only!
    @overload
    def __getitem__(self, key: Literal[b"urllist"]) -> list[bytes] | None: ...


class create_torrent(metaclass=_BoostBaseClass):
    canonical_files: int
    canonical_files_no_tail_padding: int
    merkle: int
    modification_time: int
    no_attributes: int
    optimize_alignment: int
    symlinks: int
    v1_only: int
    v2_only: int
    @overload
    def __init__(self, storage: file_storage, piece_size: int, flags: int) -> None: ...
    @overload
    def __init__(self, _fs: file_storage) -> None: ...
    @overload
    def __init__(self, ti: torrent_info) -> None: ...
    def add_collection(self, _collection: str) -> None: ...
    def add_http_seed(self, _url: str) -> None: ...
    def add_node(self, _ip: str, _port: int) -> None: ...
    def add_similar_torrent(self, _info_hash: sha1_hash) -> None: ...
    def add_tracker(self, announce_url: str, tier: int = 0) -> None: ...
    def add_url_seed(self, _url: str) -> None: ...
    def files(self) -> file_storage: ...
    def generate(self) -> TorrentFileDict: ...
    def generate_buf(self) -> bytes: ...
    def num_pieces(self) -> int: ...
    def piece_length(self) -> int: ...
    def piece_size(self, _index: int) -> int: ...
    def priv(self) -> bool: ...
    def set_comment(self, _comment: str) -> None: ...
    def set_creator(self, _creator: str) -> None: ...
    def set_file_hash(self, _index: int, _hash: bytes) -> None: ...
    def set_hash(self, _index: int, _hash: bytes) -> None: ...
    def set_priv(self, _priv: bool) -> None: ...
    def set_root_cert(self, pem: str) -> None: ...


class create_torrent_flags_t(metaclass=_BoostBaseClass):
    __instance_size__: int
    merkle: int
    modification_time: int
    optimize: int
    optimize_alignment: int
    symlinks: int
    v2_only: int


class deadline_flags_t(metaclass=_BoostBaseClass):
    __instance_size__: int
    alert_when_available: int


class deprecated_move_flags_t(int):
    always_replace_files: int
    dont_replace: int
    fail_if_exist: int

    names: Final[dict[str, int]] = {
        "always_replace_files": deprecated_move_flags_t.always_replace_files,  # noqa: F821
        "fail_if_exist": deprecated_move_flags_t.fail_if_exist,  # noqa: F821
        "dont_replace": deprecated_move_flags_t.dont_replace  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: deprecated_move_flags_t.always_replace_files,  # noqa: F821
        1: deprecated_move_flags_t.fail_if_exist,  # noqa: F821
        2: deprecated_move_flags_t.dont_replace  # noqa: F821
    }


class dht_announce_alert(alert):
    @property
    def info_hash(self) -> sha1_hash: ...
    @property
    def ip(self) -> str: ...
    @property
    def port(self) -> int: ...


class dht_bootstrap_alert(alert):
    ...


class dht_get_peers_alert(alert):
    @property
    def info_hash(self) -> sha1_hash: ...


class dht_get_peers_reply_alert(alert):
    def num_peers(self) -> int: ...
    def peers(self) -> list[tuple[str, int]]: ...
    @property
    def info_hash(self) -> sha1_hash: ...


class dht_immutable_item_alert(alert):
    @property
    def item(self) -> _Entry | None: ...
    @property
    def target(self) -> sha1_hash: ...


@type_check_only
class DhtNodedict(TypedDict):
    nid: sha1_hash
    endpoint: tuple[str, int]


class dht_live_nodes_alert(alert):
    @property
    def node_id(self) -> sha1_hash: ...
    @property
    def nodes(self) -> list[DhtNodedict]: ...
    @property
    def num_nodes(self) -> int: ...


class dht_log_alert(alert):
    def log_message(self) -> str: ...
    @property
    def module(self) -> int: ...


class dht_lookup(metaclass=_BoostBaseClass):
    __instance_size__: int
    @property
    def branch_factor(self) -> int: ...
    @property
    def outstanding_requests(self) -> int: ...
    @property
    def response(self) -> int: ...
    @property
    def timeouts(self) -> int: ...
    @property
    def type(self) -> str | None: ...


class dht_mutable_item_alert(alert):
    @property
    def authoritative(self) -> bool: ...
    @property
    def item(self) -> _Entry | None: ...
    @property
    def key(self) -> bytes: ...
    @property
    def salt(self) -> bytes: ...
    @property
    def seq(self) -> int: ...
    @property
    def signature(self) -> bytes: ...


class dht_outgoing_get_peers_alert(alert):
    @property
    def endpoint(self) -> tuple[str, int]: ...
    @property
    def info_hash(self) -> sha1_hash: ...
    @property
    def ip(self) -> tuple[str, int]: ...
    @property
    def obfuscated_info_hash(self) -> sha1_hash: ...


class dht_pkt_alert(alert):
    @property
    def pkt_buf(self) -> bytes: ...


class dht_put_alert(alert):
    @property
    def num_success(self) -> int: ...
    @property
    def public_key(self) -> bytes: ...
    @property
    def salt(self) -> bytes: ...
    @property
    def seq(self) -> int: ...
    @property
    def signature(self) -> bytes: ...
    @property
    def target(self) -> sha1_hash: ...


class tracker_alert(torrent_alert):
    def tracker_url(self) -> str: ...
    @property
    def local_endpoint(self) -> tuple[str, int]: ...
    @property
    def url(self) -> str: ...


class dht_reply_alert(tracker_alert):
    @property
    def num_peers(self) -> int: ...


class dht_sample_infohashes_alert(alert):
    @property
    def endpoint(self) -> tuple[str, int]: ...
    @property
    def interval(self) -> datetime.timedelta: ...
    @property
    def nodes(self) -> list[DhtNodedict]: ...
    @property
    def num_infohashes(self) -> int: ...
    @property
    def num_nodes(self) -> int: ...
    @property
    def num_samples(self) -> int: ...
    @property
    def samples(self) -> list[sha1_hash]: ...


class dht_settings(metaclass=_BoostBaseClass):
    __instance_size__: int
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


class dht_state(metaclass=_BoostBaseClass):
    __instance_size__: int
    @property
    def nids(self) -> list[tuple[str, sha1_hash]]: ...
    @property
    def nodes(self) -> list[tuple[str, int]]: ...
    @property
    def nodes6(self) -> list[tuple[str, int]]: ...


@type_check_only
class DhtStatsActiveRequest(TypedDict):
    type: str | None
    outstanding_requests: int
    timeouts: int
    responses: int
    branch_factor: int
    nodes_left: int
    last_sent: int
    first_timeout: int


@type_check_only
class DhtStatsRoute(TypedDict):
    num_nodes: int
    num_replacements: int


class dht_stats_alert(alert):
    @property
    def active_requests(self) -> list[DhtStatsActiveRequest]: ...
    @property
    def routing_table(self) -> list[DhtStatsRoute]: ...


class enc_level(int):
    both: int
    pe_both: int
    pe_plaintext: int
    pe_rc4: int
    plaintext: int
    rc4: int

    names: Final[dict[str, int]] = {
        "pe_rc4": enc_level.pe_rc4,  # noqa: F821
        "pe_plaintext": enc_level.pe_plaintext,  # noqa: F821
        "pe_both": enc_level.pe_both,  # noqa: F821
        "rc4": enc_level.rc4,  # noqa: F821
        "plaintext": enc_level.plaintext,  # noqa: F821
        "both": enc_level.both  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        1: enc_level.plaintext,  # noqa: F821
        2: enc_level.rc4,  # noqa: F821
        3: enc_level.both  # noqa: F821
    }


class enc_policy(int):
    disabled: int
    enabled: int
    forced: int
    pe_disabled: int
    pe_enabled: int
    pe_forced: int

    names: Final[dict[str, int]] = {
        "pe_forced": enc_policy.pe_forced,  # noqa: F821
        "pe_enabled": enc_policy.pe_enabled,  # noqa: F821
        "pe_disabled": enc_policy.pe_disabled,  # noqa: F821
        "forced": enc_policy.forced,  # noqa: F821
        "enabled": enc_policy.enabled,  # noqa: F821
        "disabled": enc_policy.disabled  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: enc_policy.forced,  # noqa: F821
        1: enc_policy.enabled,  # noqa: F821
        2: enc_policy.disabled  # noqa: F821
    }


class error_category(metaclass=_BoostBaseClass):
    def message(self, _value: int) -> str: ...
    def name(self) -> str: ...
    def __lt__(self, other: object) -> bool: ...


class error_code(metaclass=_BoostBaseClass):
    __instance_size__: int
    __safe_for_unpickling__: bool
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
    completed: int
    none: int
    paused: int
    started: int
    stopped: int

    names: Final[dict[str, int]] = {
        "none": event_t.none,  # noqa: F821
        "completed": event_t.completed,  # noqa: F821
        "started": event_t.started,  # noqa: F821
        "stopped": event_t.stopped,  # noqa: F821
        "paused": event_t.paused  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: event_t.none,  # noqa: F821
        1: event_t.completed,  # noqa: F821
        2: event_t.started,  # noqa: F821
        3: event_t.stopped,  # noqa: F821
        4: event_t.paused  # noqa: F821
    }


class external_ip_alert(alert):
    @property
    def external_address(self) -> str: ...


class fastresume_rejected_alert(torrent_alert):
    def file_path(self) -> str: ...
    @property
    def error(self) -> error_code: ...
    @property
    def msg(self) -> str: ...
    @property
    def op(self) -> operation_t: ...
    @property
    def operation(self) -> str: ...


class file_completed_alert(torrent_alert):
    @property
    def index(self) -> int: ...


class file_entry(metaclass=_BoostBaseClass):
    __instance_size__: int
    @property
    def executable_attribute(self) -> bool: ...
    @property
    def filehash(self) -> sha1_hash: ...
    @property
    def hidden_attribute(self) -> bool: ...
    @property
    def mtime(self) -> int: ...
    @property
    def offset(self) -> int: ...
    @property
    def pad_file(self) -> bool: ...
    @property
    def path(self) -> str: ...
    @property
    def size(self) -> int: ...
    @property
    def symlink_attribute(self) -> bool: ...
    @property
    def symlink_path(self) -> str: ...


class file_error_alert(torrent_alert):
    def filename(self) -> str: ...
    @property
    def error(self) -> error_code: ...
    @property
    def file(self) -> str: ...
    @property
    def msg(self) -> str: ...


class file_flags_t(metaclass=_BoostBaseClass):
    __instance_size__: int
    flag_executable: int
    flag_hidden: int
    flag_pad_file: int
    flag_symlink: int


class file_open_mode(metaclass=_BoostBaseClass):
    __instance_size__: int
    locked: int
    mmapped: int
    no_atime: int
    random_access: int
    read_only: int
    read_write: int
    rw_mask: int
    sparse: int
    write_only: int


class file_prio_alert(torrent_alert):
    ...


class file_progress_alert(torrent_alert):
    @property
    def files(self) -> list[int]: ...


class file_progress_flags_t(metaclass=_BoostBaseClass):
    __instance_size__: int
    piece_granularity: int


class file_rename_failed_alert(torrent_alert):
    @property
    def error(self) -> error_code: ...
    @property
    def index(self) -> int: ...


class file_renamed_alert(torrent_alert):
    def new_name(self) -> str: ...
    def old_name(self) -> str: ...
    @property
    def index(self) -> int: ...
    @property
    def name(self) -> str: ...


class file_slice(metaclass=_BoostBaseClass):
    __instance_size__: int
    @property
    def file_index(self) -> int: ...
    offset: int
    size: int


class file_storage(metaclass=_BoostBaseClass):
    __instance_size__: int
    flag_executable: int
    flag_hidden: int
    flag_pad_file: int
    flag_symlink: int
    @overload
    def add_file(self, entry: file_entry) -> None: ...
    @overload
    def add_file(self, path: str, size: int, flags: int = 0, mtime: int = 0, linkpath: str = "") -> None: ...
    def at(self, _index: int) -> file_entry: ...
    def file_absolute_path(self, arg2: int) -> bool: ...
    def file_flags(self, _index: int) -> int: ...
    def file_index_at_offset(self, arg2: int) -> int: ...
    def file_index_at_piece(self, arg2: int) -> int: ...
    def file_index_for_root(self, arg2: sha256_hash) -> int: ...
    def file_name(self, _index: int) -> str: ...
    def file_offset(self, _index: int) -> int: ...
    def file_path(self, idx: int, save_path: str = ...) -> str: ...
    def file_size(self, _index: int) -> int: ...
    def hash(self, _index: int) -> sha1_hash: ...
    def is_valid(self) -> bool: ...
    def name(self) -> str: ...
    def num_files(self) -> int: ...
    def num_pieces(self) -> int: ...
    def piece_index_at_file(self, arg2: int) -> int: ...
    def piece_length(self) -> int: ...
    def piece_size(self, _index: int) -> int: ...
    def rename_file(self, _index: int, _path: str) -> None: ...
    def root(self, arg2: int) -> sha256_hash: ...
    def set_name(self, _path: str) -> None: ...
    def set_num_pieces(self, _num: int) -> None: ...
    def set_piece_length(self, _len: int) -> None: ...
    def symlink(self, _index: int) -> str: ...
    def total_size(self) -> int: ...
    def v2(self) -> bool: ...
    def __iter__(self) -> Iterator[file_entry]: ...
    def __len__(self) -> int: ...


class fingerprint(metaclass=_BoostBaseClass):
    def __init__(
        self, _prefix: str, _maj: int, _min: int, _rev: int, _tag: int
    ) -> None: ...
    major_version: int
    minor_version: int
    revision_version: int
    tag_version: int


class hash_failed_alert(torrent_alert):
    @property
    def piece_index(self) -> int: ...


class i2p_alert(alert):
    @property
    def error(self) -> error_code: ...


class incoming_connection_alert(alert):
    @property
    def endpoint(self) -> tuple[str, int]: ...
    @property
    def ip(self) -> tuple[str, int]: ...
    @property
    def socket_type(self) -> socket_type_t: ...


class info_hash_t(metaclass=_BoostBaseClass):
    __instance_size__: int
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
    def __eq__(self, other: object) -> bool: ...
    def __lt__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...
    @property
    def v1(self) -> sha1_hash: ...
    @property
    def v2(self) -> sha256_hash: ...


class invalid_request_alert(peer_alert):
    @property
    def request(self) -> peer_request: ...


class io_buffer_mode_t(int):
    disable_os_cache: int
    disable_os_cache_for_aligned_files: int
    enable_os_cache: int
    write_through: int

    names: Final[dict[str, int]] = {
        "enable_os_cache": io_buffer_mode_t.enable_os_cache,  # noqa: F821
        "disable_os_cache_for_aligned_files": io_buffer_mode_t.disable_os_cache_for_aligned_files,  # noqa: F821
        "disable_os_cache": io_buffer_mode_t.disable_os_cache,  # noqa: F821
        "write_through": io_buffer_mode_t.write_through  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: io_buffer_mode_t.enable_os_cache,  # noqa: F821
        2: io_buffer_mode_t.disable_os_cache,  # noqa: F821
        3: io_buffer_mode_t.write_through  # noqa: F821
    }


class ip_filter(metaclass=_BoostBaseClass):
    __instance_size__: int
    def access(self, _ip: str) -> int: ...
    def add_rule(self, _start: str, _stop: str, _flag: int) -> None: ...
    def export_filter(self) -> tuple[list[tuple[str, str]], list[tuple[str, str]]]: ...


class kind(int):
    tracker_no_anonymous: kind

    names: Final[dict[str, int]] = {"tracker_no_anonymous": kind.tracker_no_anonymous}  # noqa: F821
    values: Final[dict[int, int]] = {0: kind.tracker_no_anonymous}  # noqa: F821


class listen_failed_alert(alert):
    def listen_interface(self) -> str: ...
    @property
    def address(self) -> str: ...
    @property
    def endpoint(self) -> tuple[str, int]: ...
    @property
    def error(self) -> error_code: ...
    @property
    def op(self) -> operation_t: ...
    @property
    def operation(self) -> int: ...
    @property
    def port(self) -> int: ...
    @property
    def sock_type(self) -> listen_failed_alert_socket_type_t: ...
    @property
    def socket_type(self) -> socket_type_t: ...


class listen_failed_alert_socket_type_t(int):
    i2p: int
    socks5: int
    tcp: int
    tcp_ssl: int
    udp: int
    utp_ssl: int

    names: Final[dict[str, int]] = {
        "tcp": listen_failed_alert_socket_type_t.tcp,  # noqa: F821
        "tcp_ssl": listen_failed_alert_socket_type_t.tcp_ssl,  # noqa: F821
        "udp": listen_failed_alert_socket_type_t.udp,  # noqa: F821
        "i2p": listen_failed_alert_socket_type_t.i2p,  # noqa: F821
        "socks5": listen_failed_alert_socket_type_t.socks5,  # noqa: F821
        "utp_ssl": listen_failed_alert_socket_type_t.utp_ssl  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: listen_failed_alert_socket_type_t.tcp,  # noqa: F821
        1: listen_failed_alert_socket_type_t.tcp_ssl,  # noqa: F821
        2: listen_failed_alert_socket_type_t.udp,  # noqa: F821
        3: listen_failed_alert_socket_type_t.i2p,  # noqa: F821
        4: listen_failed_alert_socket_type_t.socks5,  # noqa: F821
        5: listen_failed_alert_socket_type_t.utp_ssl  # noqa: F821
    }


class listen_on_flags_t(int):
    listen_no_system_port: int
    listen_reuse_address: int

    names: Final[dict[str, int]] = {
        "listen_reuse_address": listen_on_flags_t.listen_reuse_address,  # noqa: F821
        "listen_no_system_port": listen_on_flags_t.listen_no_system_port  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        1: listen_on_flags_t.listen_reuse_address,  # noqa: F821
        2: listen_on_flags_t.listen_no_system_port  # noqa: F821
    }


class listen_succeeded_alert_socket_type_t(int):
    i2p: int
    socks5: int
    tcp: int
    tcp_ssl: int
    udp: int
    utp_ssl: int

    names: Final[dict[str, int]] = {
        "tcp": listen_succeeded_alert_socket_type_t.tcp,  # noqa: F821
        "tcp_ssl": listen_succeeded_alert_socket_type_t.tcp_ssl,  # noqa: F821
        "udp": listen_succeeded_alert_socket_type_t.udp,  # noqa: F821
        "i2p": listen_succeeded_alert_socket_type_t.i2p,  # noqa: F821
        "socks5": listen_succeeded_alert_socket_type_t.socks5,  # noqa: F821
        "utp_ssl": listen_succeeded_alert_socket_type_t.utp_ssl  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: listen_succeeded_alert_socket_type_t.tcp,  # noqa: F821
        1: listen_succeeded_alert_socket_type_t.tcp_ssl,  # noqa: F821
        2: listen_succeeded_alert_socket_type_t.udp,  # noqa: F821
        3: listen_succeeded_alert_socket_type_t.i2p,  # noqa: F821
        4: listen_succeeded_alert_socket_type_t.socks5,  # noqa: F821
        5: listen_succeeded_alert_socket_type_t.utp_ssl  # noqa: F821
    }


class listen_succeeded_alert(alert):
    @property
    def address(self) -> str: ...
    @property
    def endpoint(self) -> tuple[str, int]: ...
    @property
    def port(self) -> int: ...
    @property
    def sock_type(self) -> listen_succeeded_alert_socket_type_t: ...
    @property
    def socket_type(self) -> socket_type_t: ...


class log_alert(alert):
    def log_message(self) -> str: ...
    def msg(self) -> str: ...


class lsd_error_alert(alert):
    @property
    def error(self) -> error_code: ...


class metadata_failed_alert(torrent_alert):
    @property
    def error(self) -> error_code: ...


class metadata_received_alert(torrent_alert):
    ...


class metric_type_t(int):
    counter: int
    gauge: int

    names: Final[dict[str, int]] = {
        "counter": metric_type_t.counter,  # noqa: F821
        "gauge": metric_type_t.gauge  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: metric_type_t.counter,  # noqa: F821
        1: metric_type_t.gauge  # noqa: F821
    }


class mmap_write_mode_t(int):
    always_mmap_write: int
    always_pwrite: int
    auto_mmap_write: int

    names: Final[dict[str, int]] = {
        "always_pwrite": mmap_write_mode_t.always_pwrite,  # noqa: F821
        "always_mmap_write": mmap_write_mode_t.always_mmap_write,  # noqa: F821
        "auto_mmap_write": mmap_write_mode_t.auto_mmap_write  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: mmap_write_mode_t.always_pwrite,  # noqa: F821
        1: mmap_write_mode_t.always_mmap_write,  # noqa: F821
        2: mmap_write_mode_t.auto_mmap_write  # noqa: F821
    }


class move_flags_t(int):
    always_replace_files: int
    dont_replace: int
    fail_if_exist: int
    reset_save_path: int
    reset_save_path_unchecked: int

    names: Final[dict[str, int]] = {
        "always_replace_files": move_flags_t.always_replace_files,  # noqa: F821
        "fail_if_exist": move_flags_t.fail_if_exist,  # noqa: F821
        "dont_replace": move_flags_t.dont_replace,  # noqa: F821
        "reset_save_path": move_flags_t.reset_save_path,  # noqa: F821
        "reset_save_path_unchecked": move_flags_t.reset_save_path_unchecked  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: move_flags_t.always_replace_files,  # noqa: F821
        1: move_flags_t.fail_if_exist,  # noqa: F821
        2: move_flags_t.dont_replace,  # noqa: F821
        3: move_flags_t.reset_save_path,  # noqa: F821
        4: move_flags_t.reset_save_path_unchecked  # noqa: F821
    }


class open_file_state(metaclass=_BoostBaseClass):
    __instance_size__: int
    @property
    def file_index(self) -> int: ...
    @property
    def last_use(self) -> datetime.datetime: ...
    @property
    def open_mode(self) -> file_open_mode: ...


class operation_t(int):
    alloc_cache_piece: int
    alloc_recvbuf: int
    alloc_sndbuf: int
    available: int
    bittorrent: int
    check_resume: int
    connect: int
    encryption: int
    enum_if: int
    exception: int
    file: int
    file_copy: int
    file_fallocate: int
    file_hard_link: int
    file_open: int
    file_read: int
    file_remove: int
    file_rename: int
    file_stat: int
    file_write: int
    get_interface: int
    getname: int
    getpeername: int
    handshake: int
    hostname_lookup: int
    iocontrol: int
    mkdir: int
    parse_address: int
    partfile_move: int
    partfile_read: int
    partfile_write: int
    sock_accept: int
    sock_bind: int
    sock_bind_to_device: int
    sock_listen: int
    sock_open: int
    sock_option: int
    sock_read: int
    sock_write: int
    ssl_handshake: int
    symlink: int
    unknown: int

    names: Final[dict[str, int]] = {
        "unknown": operation_t.unknown,  # noqa: F821
        "bittorrent": operation_t.bittorrent,  # noqa: F821
        "iocontrol": operation_t.iocontrol,  # noqa: F821
        "getpeername": operation_t.getpeername,  # noqa: F821
        "getname": operation_t.getname,  # noqa: F821
        "alloc_recvbuf": operation_t.alloc_recvbuf,  # noqa: F821
        "alloc_sndbuf": operation_t.alloc_sndbuf,  # noqa: F821
        "file_write": operation_t.file_write,  # noqa: F821
        "file_read": operation_t.file_read,  # noqa: F821
        "file": operation_t.file,  # noqa: F821
        "sock_write": operation_t.sock_write,  # noqa: F821
        "sock_read": operation_t.sock_read,  # noqa: F821
        "sock_open": operation_t.sock_open,  # noqa: F821
        "sock_bind": operation_t.sock_bind,  # noqa: F821
        "available": operation_t.available,  # noqa: F821
        "encryption": operation_t.encryption,  # noqa: F821
        "connect": operation_t.connect,  # noqa: F821
        "ssl_handshake": operation_t.ssl_handshake,  # noqa: F821
        "get_interface": operation_t.get_interface,  # noqa: F821
        "sock_listen": operation_t.sock_listen,  # noqa: F821
        "sock_bind_to_device": operation_t.sock_bind_to_device,  # noqa: F821
        "sock_accept": operation_t.sock_accept,  # noqa: F821
        "parse_address": operation_t.parse_address,  # noqa: F821
        "enum_if": operation_t.enum_if,  # noqa: F821
        "file_stat": operation_t.file_stat,  # noqa: F821
        "file_copy": operation_t.file_copy,  # noqa: F821
        "file_fallocate": operation_t.file_fallocate,  # noqa: F821
        "file_hard_link": operation_t.file_hard_link,  # noqa: F821
        "file_remove": operation_t.file_remove,  # noqa: F821
        "file_rename": operation_t.file_rename,  # noqa: F821
        "file_open": operation_t.file_open,  # noqa: F821
        "mkdir": operation_t.mkdir,  # noqa: F821
        "check_resume": operation_t.check_resume,  # noqa: F821
        "exception": operation_t.exception,  # noqa: F821
        "alloc_cache_piece": operation_t.alloc_cache_piece,  # noqa: F821
        "partfile_move": operation_t.partfile_move,  # noqa: F821
        "partfile_read": operation_t.partfile_read,  # noqa: F821
        "partfile_write": operation_t.partfile_write,  # noqa: F821
        "hostname_lookup": operation_t.hostname_lookup,  # noqa: F821
        "symlink": operation_t.symlink,  # noqa: F821
        "handshake": operation_t.handshake,  # noqa: F821
        "sock_option": operation_t.sock_option  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: operation_t.unknown,  # noqa: F821
        1: operation_t.bittorrent,  # noqa: F821
        2: operation_t.iocontrol,  # noqa: F821
        3: operation_t.getpeername,  # noqa: F821
        4: operation_t.getname,  # noqa: F821
        5: operation_t.alloc_recvbuf,  # noqa: F821
        6: operation_t.alloc_sndbuf,  # noqa: F821
        7: operation_t.file_write,  # noqa: F821
        8: operation_t.file_read,  # noqa: F821
        9: operation_t.file,  # noqa: F821
        10: operation_t.sock_write,  # noqa: F821
        11: operation_t.sock_read,  # noqa: F821
        12: operation_t.sock_open,  # noqa: F821
        13: operation_t.sock_bind,  # noqa: F821
        14: operation_t.available,  # noqa: F821
        15: operation_t.encryption,  # noqa: F821
        16: operation_t.connect,  # noqa: F821
        17: operation_t.ssl_handshake,  # noqa: F821
        18: operation_t.get_interface,  # noqa: F821
        19: operation_t.sock_listen,  # noqa: F821
        20: operation_t.sock_bind_to_device,  # noqa: F821
        21: operation_t.sock_accept,  # noqa: F821
        22: operation_t.parse_address,  # noqa: F821
        23: operation_t.enum_if,  # noqa: F821
        24: operation_t.file_stat,  # noqa: F821
        25: operation_t.file_copy,  # noqa: F821
        26: operation_t.file_fallocate,  # noqa: F821
        27: operation_t.file_hard_link,  # noqa: F821
        28: operation_t.file_remove,  # noqa: F821
        29: operation_t.file_rename,  # noqa: F821
        30: operation_t.file_open,  # noqa: F821
        31: operation_t.mkdir,  # noqa: F821
        32: operation_t.check_resume,  # noqa: F821
        33: operation_t.exception,  # noqa: F821
        34: operation_t.alloc_cache_piece,  # noqa: F821
        35: operation_t.partfile_move,  # noqa: F821
        36: operation_t.partfile_read,  # noqa: F821
        37: operation_t.partfile_write,  # noqa: F821
        38: operation_t.hostname_lookup,  # noqa: F821
        39: operation_t.symlink,  # noqa: F821
        40: operation_t.handshake,  # noqa: F821
        41: operation_t.sock_option  # noqa: F821
    }


class options_t(metaclass=_BoostBaseClass):
    __instance_size__: int
    delete_files: int


class oversized_file_alert(torrent_alert):
    ...


class pause_flags_t(metaclass=_BoostBaseClass):
    __instance_size__: int
    graceful_pause: int


class pe_settings(metaclass=_BoostBaseClass):
    __instance_size__: int
    allowed_enc_level: int
    in_enc_policy: int
    out_enc_policy: int
    prefer_rc4: bool


class peer_ban_alert(peer_alert):
    ...


class peer_blocked_alert(peer_alert):
    @property
    def reason(self) -> reason_t: ...


class peer_class_type_filter(metaclass=_BoostBaseClass):
    __instance_size__: int
    i2p_socket: peer_class_type_filter_socket_type_t
    ssl_tcp_socket: peer_class_type_filter_socket_type_t
    ssl_utp_socket: peer_class_type_filter_socket_type_t
    tcp_socket: peer_class_type_filter_socket_type_t
    utp_socket: peer_class_type_filter_socket_type_t
    def add(self, _type: peer_class_type_filter_socket_type_t, _class: int) -> None: ...
    def allow(self, _type: peer_class_type_filter_socket_type_t, _class: int) -> None: ...
    def apply(self, _type: peer_class_type_filter_socket_type_t, _class: int) -> int: ...
    def disallow(self, _type: peer_class_type_filter_socket_type_t, _class: int) -> None: ...
    def remove(self, _type: peer_class_type_filter_socket_type_t, _class: int) -> None: ...


class peer_class_type_filter_socket_type_t(int):
    i2p_socket: int
    ssl_tcp_socket: int
    ssl_utp_socket: int
    tcp_socket: int
    utp_socket: int

    names: Final[dict[str, int]] = {
        "tcp_socket": peer_class_type_filter_socket_type_t.tcp_socket,  # noqa: F821
        "utp_socket": peer_class_type_filter_socket_type_t.utp_socket,  # noqa: F821
        "ssl_tcp_socket": peer_class_type_filter_socket_type_t.ssl_tcp_socket,  # noqa: F821
        "ssl_utp_socket": peer_class_type_filter_socket_type_t.ssl_utp_socket,  # noqa: F821
        "i2p_socket": peer_class_type_filter_socket_type_t.i2p_socket  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: peer_class_type_filter_socket_type_t.tcp_socket,  # noqa: F821
        1: peer_class_type_filter_socket_type_t.utp_socket,  # noqa: F821
        2: peer_class_type_filter_socket_type_t.ssl_tcp_socket,  # noqa: F821
        3: peer_class_type_filter_socket_type_t.ssl_utp_socket,  # noqa: F821
        4: peer_class_type_filter_socket_type_t.i2p_socket  # noqa: F821
    }


class peer_connect_alert(peer_alert):
    ...


class peer_disconnected_alert(peer_alert):
    @property
    def error(self) -> error_code: ...
    @property
    def msg(self) -> str: ...
    @property
    def op(self) -> operation_t: ...
    @property
    def reason(self) -> int: ...
    @property
    def socket_type(self) -> socket_type_t: ...


class peer_error_alert(peer_alert):
    @property
    def error(self) -> error_code: ...
    @property
    def op(self) -> operation_t: ...


peer_id = sha1_hash


class peer_info(metaclass=_BoostBaseClass):
    __instance_size__: int
    i2p_socket: int
    bw_disk: Literal[16]
    bw_global: Literal[2]
    bw_idle: Literal[1]
    bw_limit: Literal[2]
    bw_network: Literal[4]
    bw_torrent: Literal[2]
    choked: Literal[2]
    connecting: Literal[128]
    dht: Literal[2]
    endgame_mode: Literal[16384]
    handshake: Literal[64]
    holepunched: Literal[32768]
    http_seed: Literal[4]
    interesting: Literal[1]
    local_connection: Literal[32]
    lsd: Literal[8]
    on_parole: Literal[512]
    optimistic_unchoke: Literal[2048]
    outgoing_connection: Literal[32]
    pex: Literal[4]
    plaintext_encrypted: Literal[1048576]
    queued: Literal[256]
    rc4_encrypted: Literal[524288]
    remote_choked: Literal[8]
    remote_interested: Literal[4]
    resume_data: Literal[16]
    seed: Literal[1024]
    snubbed: Literal[4096]
    standard_bittorrent: Literal[1]
    supports_extensions: Literal[16]
    tracker: Literal[1]
    upload_only: Literal[8192]
    web_seed: Literal[2]

    def i2p_destination(self) -> sha256_hash: ...
    @property
    def client(self) -> bytes: ...
    @property
    def connection_type(self) -> int: ...
    @property
    def down_speed(self) -> int: ...
    @property
    def download_limit(self) -> int: ...
    @property
    def download_queue_length(self) -> int: ...
    @property
    def download_queue_time(self) -> int: ...
    @property
    def download_rate_peak(self) -> int: ...
    @property
    def downloading_block_index(self) -> int: ...
    @property
    def downloading_piece_index(self) -> int: ...
    @property
    def downloading_progress(self) -> int: ...
    @property
    def downloading_total(self) -> int: ...
    @property
    def estimated_reciprocation_rate(self) -> int: ...
    @property
    def failcount(self) -> int: ...
    @property
    def flags(self) -> int: ...
    @property
    def ip(self) -> tuple[str, int]: ...
    @property
    def last_active(self) -> int: ...
    @property
    def last_request(self) -> int: ...
    @property
    def load_balancing(self) -> int: ...
    @property
    def local_endpoint(self) -> tuple[str, int]: ...
    @property
    def num_hashfails(self) -> int: ...
    @property
    def num_pieces(self) -> int: ...
    @property
    def payload_down_speed(self) -> int: ...
    @property
    def payload_up_speed(self) -> int: ...
    @property
    def pending_disk_bytes(self) -> int: ...
    @property
    def pid(self) -> sha1_hash: ...
    @property
    def pieces(self) -> list[bool]: ...
    @property
    def progress(self) -> float: ...
    @property
    def progress_ppm(self) -> int: ...
    @property
    def queue_bytes(self) -> int: ...
    @property
    def read_state(self) -> int: ...
    @property
    def receive_buffer_size(self) -> int: ...
    @property
    def receive_quota(self) -> int: ...
    @property
    def remote_dl_rate(self) -> int: ...
    @property
    def request_timeout(self) -> int: ...
    @property
    def rtt(self) -> int: ...
    @property
    def send_buffer_size(self) -> int: ...
    @property
    def send_quota(self) -> int: ...
    @property
    def source(self) -> int: ...
    @property
    def total_download(self) -> int: ...
    @property
    def total_upload(self) -> int: ...
    @property
    def up_speed(self) -> int: ...
    @property
    def upload_limit(self) -> int: ...
    @property
    def upload_queue_length(self) -> int: ...
    @property
    def upload_rate_peak(self) -> int: ...
    @property
    def used_receive_buffer(self) -> int: ...
    @property
    def used_send_buffer(self) -> int: ...
    @property
    def write_state(self) -> int: ...


class peer_info_alert(peer_alert):
    @property
    def peer_info(self) -> list[peer_info]: ...


class peer_log_alert(peer_alert):
    def log_message(self) -> str: ...
    def msg(self) -> str: ...


class peer_request(metaclass=_BoostBaseClass):
    __instance_size__: int
    def __eq__(self, other: object) -> bool: ...
    @property
    def length(self) -> int: ...
    @property
    def piece(self) -> int: ...
    @property
    def start(self) -> int: ...


class peer_snubbed_alert(peer_alert):
    ...


class peer_unsnubbed_alert(peer_alert):
    ...


class performance_alert(torrent_alert):
    @property
    def warning_code(self) -> performance_warning_t: ...


class performance_warning_t(int):
    bittyrant_with_no_uplimit: int
    download_limit_too_low: int
    outstanding_disk_buffer_limit_reached: int
    outstanding_request_limit_reached: int
    send_buffer_watermark_too_low: int
    too_few_file_descriptors: int
    too_few_outgoing_ports: int
    too_high_disk_queue_limit: int
    too_many_optimistic_unchoke_slots: int
    upload_limit_too_low: int

    names: Final[dict[str, int]] = {
        "outstanding_disk_buffer_limit_reached": performance_warning_t.outstanding_disk_buffer_limit_reached,  # noqa: F821
        "outstanding_request_limit_reached": performance_warning_t.outstanding_request_limit_reached,  # noqa: F821
        "upload_limit_too_low": performance_warning_t.upload_limit_too_low,  # noqa: F821
        "download_limit_too_low": performance_warning_t.download_limit_too_low,  # noqa: F821
        "send_buffer_watermark_too_low": performance_warning_t.send_buffer_watermark_too_low,  # noqa: F821
        "too_many_optimistic_unchoke_slots": performance_warning_t.too_many_optimistic_unchoke_slots,  # noqa: F821
        "bittyrant_with_no_uplimit": performance_warning_t.bittyrant_with_no_uplimit,  # noqa: F821
        "too_high_disk_queue_limit": performance_warning_t.too_high_disk_queue_limit,  # noqa: F821
        "too_few_outgoing_ports": performance_warning_t.too_few_outgoing_ports,  # noqa: F821
        "too_few_file_descriptors": performance_warning_t.too_few_file_descriptors  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: performance_warning_t.outstanding_disk_buffer_limit_reached,  # noqa: F821
        1: performance_warning_t.outstanding_request_limit_reached,  # noqa: F821
        2: performance_warning_t.upload_limit_too_low,  # noqa: F821
        3: performance_warning_t.download_limit_too_low,  # noqa: F821
        4: performance_warning_t.send_buffer_watermark_too_low,  # noqa: F821
        5: performance_warning_t.too_many_optimistic_unchoke_slots,  # noqa: F821
        8: performance_warning_t.bittyrant_with_no_uplimit,  # noqa: F821
        6: performance_warning_t.too_high_disk_queue_limit,  # noqa: F821
        9: performance_warning_t.too_few_outgoing_ports,  # noqa: F821
        10: performance_warning_t.too_few_file_descriptors  # noqa: F821
    }


class picker_flags_t(int):
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

    names: Final[dict[str, int]] = {
        "partial_ratio": picker_flags_t.partial_ratio,  # noqa: F821
        "prioritize_partials": picker_flags_t.prioritize_partials,  # noqa: F821
        "rarest_first_partials": picker_flags_t.rarest_first_partials,  # noqa: F821
        "rarest_first": picker_flags_t.rarest_first,  # noqa: F821
        "reverse_rarest_first": picker_flags_t.reverse_rarest_first,  # noqa: F821
        "suggested_pieces": picker_flags_t.suggested_pieces,  # noqa: F821
        "prio_sequential_pieces": picker_flags_t.prio_sequential_pieces,  # noqa: F821
        "sequential_pieces": picker_flags_t.sequential_pieces,  # noqa: F821
        "reverse_pieces": picker_flags_t.reverse_pieces,  # noqa: F821
        "time_critical": picker_flags_t.time_critical,  # noqa: F821
        "random_pieces": picker_flags_t.random_pieces,  # noqa: F821
        "prefer_contiguous": picker_flags_t.prefer_contiguous,  # noqa: F821
        "reverse_sequential": picker_flags_t.reverse_sequential,  # noqa: F821
        "backup1": picker_flags_t.backup1,  # noqa: F821
        "backup2": picker_flags_t.backup2,  # noqa: F821
        "end_game": picker_flags_t.end_game,  # noqa: F821
        "extent_affinity": picker_flags_t.extent_affinity  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        1: picker_flags_t.partial_ratio,  # noqa: F821
        2: picker_flags_t.prioritize_partials,  # noqa: F821
        4: picker_flags_t.rarest_first_partials,  # noqa: F821
        8: picker_flags_t.rarest_first,  # noqa: F821
        16: picker_flags_t.reverse_rarest_first,  # noqa: F821
        32: picker_flags_t.suggested_pieces,  # noqa: F821
        64: picker_flags_t.prio_sequential_pieces,  # noqa: F821
        128: picker_flags_t.sequential_pieces,  # noqa: F821
        256: picker_flags_t.reverse_pieces,  # noqa: F821
        512: picker_flags_t.time_critical,  # noqa: F821
        1024: picker_flags_t.random_pieces,  # noqa: F821
        2048: picker_flags_t.prefer_contiguous,  # noqa: F821
        4096: picker_flags_t.reverse_sequential,  # noqa: F821
        8192: picker_flags_t.backup1,  # noqa: F821
        16384: picker_flags_t.backup2,  # noqa: F821
        32768: picker_flags_t.end_game,  # noqa: F821
        65536: picker_flags_t.extent_affinity  # noqa: F821
    }


class picker_log_alert(peer_alert):
    def blocks(self) -> list[int]: ...
    @property
    def picker_flags(self) -> int: ...


class piece_availability_alert(torrent_alert):
    @property
    def piece_availability(self) -> list[int]: ...


class piece_info_alert(torrent_alert):
    @property
    def piece_info(self) -> list[PartialPieceInfodict]: ...


class piece_finished_alert(torrent_alert):
    @property
    def piece_index(self) -> int: ...


class portmap_alert(alert):
    @property
    def external_port(self) -> int: ...
    @property
    def map_protocol(self) -> portmap_protocol: ...
    @property
    def map_transport(self) -> portmap_transport: ...
    @property
    def map_type(self) -> int: ...
    @property
    def mapping(self) -> int: ...
    @property
    def type(self) -> int: ...


class portmap_error_alert(alert):
    @property
    def error(self) -> error_code: ...
    @property
    def map_transport(self) -> portmap_transport: ...
    @property
    def map_type(self) -> int: ...
    @property
    def mapping(self) -> int: ...
    @property
    def msg(self) -> str: ...
    @property
    def type(self) -> int: ...


class portmap_log_alert(alert):
    @property
    def map_transport(self) -> portmap_transport: ...
    @property
    def map_type(self) -> int: ...
    @property
    def msg(self) -> str: ...
    @property
    def type(self) -> int: ...


class portmap_protocol(int):
    none: int
    tcp: int
    udp: int

    names: Final[dict[str, int]] = {
        "none": portmap_protocol.none,  # noqa: F821
        "udp": portmap_protocol.udp,  # noqa: F821
        "tcp": portmap_protocol.tcp,  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: portmap_protocol.none,  # noqa: F821
        1: portmap_protocol.tcp,  # noqa: F821
        2: portmap_protocol.udp  # noqa: F821
    }


class portmap_transport(int):
    natpmp: int
    upnp: int

    names: Final[dict[str, int]] = {
        "natpmp": portmap_transport.natpmp,  # noqa: F821
        "upnp": portmap_transport.upnp  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: portmap_transport.natpmp,  # noqa: F821
        1: portmap_transport.upnp  # noqa: F821
    }


class protocol_type(metaclass=_BoostBaseClass):
    __instance_size__: int
    tcp: portmap_protocol
    udp: portmap_protocol


class protocol_version(int):
    V1: int
    V2: int

    names: Final[dict[str, int]] = {
        "V1": protocol_version.V1,  # noqa: F821
        "V2": protocol_version.V2  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: protocol_version.V1,  # noqa: F821
        1: protocol_version.V2  # noqa: F821
    }


class proxy_type_t(int):
    proxy_type: type[proxy_type_t]

    http: int
    http_pw: int
    i2p_proxy: int
    none: int
    socks4: int
    socks5: int
    socks5_pw: int

    class proxy_settings(metaclass=_BoostBaseClass):
        __instance_size__: int
        hostname: str
        port: int
        password: str
        username: str
        type: proxy_type_t
        proxy_peer_connections: bool
        proxy_hostnames: bool

    names: Final[dict[str, int]] = {
        "none": proxy_type_t.none,  # noqa: F821
        "socks4": proxy_type_t.socks4,  # noqa: F821
        "socks5": proxy_type_t.socks5,  # noqa: F821
        "socks5_pw": proxy_type_t.socks5_pw,  # noqa: F821
        "http": proxy_type_t.http,  # noqa: F821
        "http_pw": proxy_type_t.http_pw,  # noqa: F821
        "i2p_proxy": proxy_type_t.i2p_proxy  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: proxy_type_t.none,  # noqa: F821
        1: proxy_type_t.socks4,  # noqa: F821
        2: proxy_type_t.socks5,  # noqa: F821
        3: proxy_type_t.socks5_pw,  # noqa: F821
        4: proxy_type_t.http,  # noqa: F821
        5: proxy_type_t.http_pw,  # noqa: F821
        6: proxy_type_t.i2p_proxy  # noqa: F821
    }


class read_piece_alert(torrent_alert):
    @property
    def buffer(self) -> bytes: ...
    @property
    def ec(self) -> error_code: ...
    @property
    def error(self) -> error_code: ...
    @property
    def piece(self) -> int: ...
    @property
    def size(self) -> int: ...


class reannounce_flags_t(metaclass=_BoostBaseClass):
    __instance_size__: int
    ignore_min_interval: int


class reason_t(int):
    i2p_mixed: int
    invalid_local_interface: int
    ip_filter: int
    port_filter: int
    privileged_ports: int
    tcp_disabled: int
    utp_disabled: int

    names: Final[dict[str, int]] = {
        "ip_filter": reason_t.ip_filter,  # noqa: F821
        "port_filter": reason_t.port_filter,  # noqa: F821
        "i2p_mixed": reason_t.i2p_mixed,  # noqa: F821
        "privileged_ports": reason_t.privileged_ports,  # noqa: F821
        "utp_disabled": reason_t.utp_disabled,  # noqa: F821
        "tcp_disabled": reason_t.tcp_disabled,  # noqa: F821
        "invalid_local_interface": reason_t.invalid_local_interface  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: reason_t.ip_filter,  # noqa: F821
        1: reason_t.port_filter,  # noqa: F821
        2: reason_t.i2p_mixed,  # noqa: F821
        3: reason_t.privileged_ports,  # noqa: F821
        4: reason_t.utp_disabled,  # noqa: F821
        5: reason_t.tcp_disabled,  # noqa: F821
        6: reason_t.invalid_local_interface  # noqa: F821
    }


class request_dropped_alert(peer_alert):
    @property
    def block_index(self) -> int: ...
    @property
    def piece_index(self) -> int: ...


class save_resume_data_alert(torrent_alert):
    @property
    def params(self) -> add_torrent_params: ...
    @property
    def resume_data(self) -> bytes: ...


class save_resume_data_failed_alert(torrent_alert):
    @property
    def error(self) -> error_code: ...
    @property
    def msg(self) -> str: ...


class save_resume_flags_t(metaclass=_BoostBaseClass):
    __instance_size__: int
    flush_disk_cache: int
    only_if_modified: int
    save_info_dict: int


class save_state_flags_t(metaclass=_BoostBaseClass):
    __instance_size__: int
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
    @property
    def error(self) -> error_code: ...
    @property
    def msg(self) -> str: ...


class scrape_reply_alert(tracker_alert):
    @property
    def complete(self) -> int: ...
    @property
    def incomplete(self) -> int: ...


class seed_choking_algorithm_t(int):
    anti_leech: int
    fastest_upload: int
    round_robin: int

    names: Final[dict[str, int]] = {
        "round_robin": seed_choking_algorithm_t.round_robin,  # noqa: F821
        "fastest_upload": seed_choking_algorithm_t.fastest_upload,  # noqa: F821
        "anti_leech": seed_choking_algorithm_t.anti_leech  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: seed_choking_algorithm_t.round_robin,  # noqa: F821
        1: seed_choking_algorithm_t.fastest_upload,  # noqa: F821
        2: seed_choking_algorithm_t.anti_leech  # noqa: F821
    }


@type_check_only
class PeerClassInfo(TypedDict):
    ignore_unchoke_slots: bool
    connection_limit_factor: int
    label: str
    upload_limit: int
    download_limit: int
    upload_priority: int
    download_priority: int


@type_check_only
class DeprecatedDHTState(dict):
    @overload
    def __getitem__(self, key: Literal[b"node-id"]) -> list[bytes]: ...
    @overload
    def __getitem__(self, key: Literal[b"nodes"]) -> list[bytes]: ...


class storage_mode_t(int):
    storage_mode_allocate: int
    storage_mode_sparse: int

    names: Final[dict[str, int]] = {
        "storage_mode_allocate": storage_mode_t.storage_mode_allocate,  # noqa: F821
        "storage_mode_sparse": storage_mode_t.storage_mode_sparse  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: storage_mode_t.storage_mode_allocate,  # noqa: F821
        1: storage_mode_t.storage_mode_sparse  # noqa: F821
    }


class session(metaclass=_BoostBaseClass):
    delete_files: int
    delete_partfile: int
    global_peer_class_id: int
    local_peer_class_id: int
    reopen_map_ports: int
    tcp: portmap_protocol
    tcp_peer_class_id: int
    udp: portmap_protocol
    @overload
    def __init__(self, settings: session_params | None = None) -> None: ...
    @overload
    def __init__(self, settings: settings_pack, flags: int = 1) -> None: ...
    @overload
    def __init__(self, fingerprint: fingerprint | None = None, flags: int = 3, alert_mask: int = 1) -> None: ...
    def add_dht_node(self, _endpoint: tuple[str, int]) -> None: ...
    def add_dht_router(self, router: str, port: int) -> None: ...
    def add_extension(self, extension: Literal["create_smart_ban_plugin", "create_ut_metadata_plugin", "create_ut_pex_plugin"]) -> None: ...
    def add_port_mapping(self, _proto: portmap_protocol, _int: int, _ext: int) -> list[int]: ...
    @overload
    def add_torrent(self, ti: torrent_info, save: str, resume_data: ResumeDataDict | None = None, storage_mode: int = storage_mode_t.storage_mode_sparse, paused: bool = False) -> torrent_handle: ...
    @overload
    def add_torrent(self, arg2: add_torrent_params) -> torrent_handle: ...
    @overload
    def add_torrent(self, _bdecoded: AddTorrentParamsdict) -> torrent_handle: ...
    def apply_settings(self, settings: settings_pack) -> None: ...
    @overload
    def async_add_torrent(self, _atp: add_torrent_params) -> None: ...
    @overload
    def async_add_torrent(self, _bdecoded: AddTorrentParamsdict) -> None: ...
    def create_peer_class(self, _name: str) -> int: ...
    def delete_peer_class(self, _class: int) -> None: ...
    def delete_port_mapping(self, _mapping: int) -> None: ...
    def dht_announce(self, info_hash: sha1_hash, port: int, flags: int) -> None: ...
    def dht_get_immutable_item(self, _target: sha1_hash) -> None: ...
    def dht_get_mutable_item(self, _key: bytes, _salt: bytes) -> None: ...
    def dht_get_peers(self, _info_hash: sha1_hash) -> None: ...
    def dht_live_nodes(self, _info_hash: sha1_hash) -> None: ...
    def dht_proxy(self) -> proxy_type_t.proxy_settings: ...
    def dht_put_immutable_item(self, _entry: _Entry) -> sha1_hash: ...
    def dht_put_mutable_item(self, _private: bytes, _public: bytes, _data: bytes, _salt: bytes) -> None: ...
    def dht_sample_infohashes(self, _endpoint: tuple[str, int], _target: sha1_hash) -> None: ...
    def dht_state(self) -> DeprecatedDHTState: ...
    def download_rate_limit(self) -> int: ...
    def find_torrent(self, _info_hash: sha1_hash) -> torrent_handle: ...
    def get_dht_settings(self) -> dht_settings: ...
    def get_ip_filter(self) -> ip_filter: ...
    def get_pe_settings(self) -> pe_settings: ...
    def get_peer_class(self, _class: int) -> PeerClassInfo: ...
    def get_settings(self) -> settings_pack: ...
    def get_torrent_status(self, pred: Callable[[torrent_status], bool], flags: int) -> list[torrent_status]: ...
    def get_torrents(self) -> list[torrent_handle]: ...
    def i2p_proxy(self) -> proxy_type_t.proxy_settings: ...
    def id(self) -> sha1_hash: ...
    def is_dht_running(self) -> bool: ...
    def is_listening(self) -> bool: ...
    def is_paused(self) -> bool: ...
    def listen_on(self, min: int, max: int, interface: str | None, flags: int) -> None: ...
    def listen_port(self) -> int: ...
    def load_state(self, entry: SessionParamsDict, flags: int = 4294967295) -> None: ...
    def local_download_rate_limit(self) -> int: ...
    def local_upload_rate_limit(self) -> int: ...
    def max_connections(self) -> int: ...
    def num_connections(self) -> int: ...
    def outgoing_ports(self, _min: int, _max: int) -> None: ...
    def pause(self) -> None: ...
    def peer_proxy(self) -> proxy_type_t.proxy_settings: ...
    def pop_alerts(self) -> list[alert]: ...
    def post_dht_stats(self) -> None: ...
    def post_session_stats(self) -> None: ...
    def post_torrent_updates(self, flags: int = ...) -> None: ...
    def proxy(self) -> proxy_type_t.proxy_settings: ...
    def refresh_torrent_status(self, torrents: list[torrent_status], flags: int) -> list[torrent_status]: ...
    def remove_torrent(self, _handle: torrent_handle, option: int = ...) -> None: ...
    def reopen_network_sockets(self, _options: int) -> None: ...
    def resume(self) -> None: ...
    def save_state(self, flags: int = 4294967295) -> SessionParamsDict: ...
    def session_state(self, flags: int = 4294967295) -> session_params: ...
    def set_alert_fd(self, _fd: int) -> None: ...
    def set_alert_mask(self, _mask: int) -> None: ...
    def set_alert_notify(self, _callback: Callable[[], None]) -> None: ...
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
    def set_peer_class(self, _class: int, _info: PeerClassInfo) -> None: ...
    def set_peer_class_filter(self, _filter: ip_filter) -> None: ...
    def set_peer_class_type_filter(self, _pctf: peer_class_type_filter) -> None: ...
    def set_peer_id(self, _pid: sha1_hash) -> None: ...
    def set_peer_proxy(self, _settings: proxy_type_t.proxy_settings) -> None: ...
    def set_proxy(self, _settings: proxy_type_t.proxy_settings) -> None: ...
    def set_tracker_proxy(self, _settings: proxy_type_t.proxy_settings) -> None: ...
    def set_upload_rate_limit(self, _rate: int) -> None: ...
    def set_web_seed_proxy(self, _settings: proxy_type_t.proxy_settings) -> None: ...
    def ssl_listen_port(self) -> int: ...
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
    def wait_for_alert(self, _ms: int) -> alert | None: ...
    def web_seed_proxy(self) -> proxy_type_t.proxy_settings: ...


class session_flags_t(metaclass=_BoostBaseClass):
    __instance_size__: int
    add_default_plugins: int
    paused: int
    start_default_features: int


@type_check_only
class settings_pack(TypedDict):
    user_agent: NotRequired[str]
    outgoing_interfaces: NotRequired[str]
    listen_interfaces: NotRequired[str]
    proxy_hostname: NotRequired[str]
    proxy_username: NotRequired[str]
    proxy_password: NotRequired[str]
    i2p_hostname: NotRequired[str]
    peer_fingerprint: NotRequired[str]
    dht_bootstrap_nodes: NotRequired[str]
    tracker_completion_timeout: NotRequired[int]
    tracker_receive_timeout: NotRequired[int]
    stop_tracker_timeout: NotRequired[int]
    tracker_maximum_response_length: NotRequired[int]
    piece_timeout: NotRequired[int]
    request_timeout: NotRequired[int]
    request_queue_time: NotRequired[int]
    max_allowed_in_request_queue: NotRequired[int]
    max_out_request_queue: NotRequired[int]
    whole_pieces_threshold: NotRequired[int]
    peer_timeout: NotRequired[int]
    urlseed_timeout: NotRequired[int]
    urlseed_pipeline_size: NotRequired[int]
    urlseed_wait_retry: NotRequired[int]
    file_pool_size: NotRequired[int]
    max_failcount: NotRequired[int]
    min_reconnect_time: NotRequired[int]
    peer_connect_timeout: NotRequired[int]
    connection_speed: NotRequired[int]
    inactivity_timeout: NotRequired[int]
    unchoke_interval: NotRequired[int]
    optimistic_unchoke_interval: NotRequired[int]
    num_want: NotRequired[int]
    initial_picker_threshold: NotRequired[int]
    allowed_fast_set_size: NotRequired[int]
    suggest_mode: NotRequired[int]
    max_queued_disk_bytes: NotRequired[int]
    handshake_timeout: NotRequired[int]
    send_buffer_low_watermark: NotRequired[int]
    send_buffer_watermark: NotRequired[int]
    send_buffer_watermark_factor: NotRequired[int]
    choking_algorithm: NotRequired[int]
    seed_choking_algorithm: NotRequired[int]
    cache_size: NotRequired[int]
    cache_buffer_chunk_size: NotRequired[int]
    cache_expiry: NotRequired[int]
    disk_io_write_mode: NotRequired[int]
    disk_io_read_mode: NotRequired[int]
    outgoing_port: NotRequired[int]
    num_outgoing_ports: NotRequired[int]
    peer_dscp: NotRequired[int]
    active_downloads: NotRequired[int]
    active_seeds: NotRequired[int]
    active_checking: NotRequired[int]
    active_dht_limit: NotRequired[int]
    active_tracker_limit: NotRequired[int]
    active_lsd_limit: NotRequired[int]
    active_limit: NotRequired[int]
    active_loaded_limit: NotRequired[int]
    auto_manage_interval: NotRequired[int]
    seed_time_limit: NotRequired[int]
    auto_scrape_interval: NotRequired[int]
    auto_scrape_min_interval: NotRequired[int]
    max_peerlist_size: NotRequired[int]
    max_paused_peerlist_size: NotRequired[int]
    min_announce_interval: NotRequired[int]
    auto_manage_startup: NotRequired[int]
    seeding_piece_quota: NotRequired[int]
    max_rejects: NotRequired[int]
    recv_socket_buffer_size: NotRequired[int]
    send_socket_buffer_size: NotRequired[int]
    max_peer_recv_buffer_size: NotRequired[int]
    file_checks_delay_per_block: NotRequired[int]
    read_cache_line_size: NotRequired[int]
    write_cache_line_size: NotRequired[int]
    optimistic_disk_retry: NotRequired[int]
    max_suggest_pieces: NotRequired[int]
    local_service_announce_interval: NotRequired[int]
    dht_announce_interval: NotRequired[int]
    udp_tracker_token_expiry: NotRequired[int]
    default_cache_min_age: NotRequired[int]
    num_optimistic_unchoke_slots: NotRequired[int]
    default_est_reciprocation_rate: NotRequired[int]
    increase_est_reciprocation_rate: NotRequired[int]
    decrease_est_reciprocation_rate: NotRequired[int]
    max_pex_peers: NotRequired[int]
    tick_interval: NotRequired[int]
    share_mode_target: NotRequired[int]
    upload_rate_limit: NotRequired[int]
    download_rate_limit: NotRequired[int]
    local_upload_rate_limit: NotRequired[int]
    local_download_rate_limit: NotRequired[int]
    dht_upload_rate_limit: NotRequired[int]
    unchoke_slots_limit: NotRequired[int]
    half_open_limit: NotRequired[int]
    connections_limit: NotRequired[int]
    connections_slack: NotRequired[int]
    utp_target_delay: NotRequired[int]
    utp_gain_factor: NotRequired[int]
    utp_min_timeout: NotRequired[int]
    utp_syn_resends: NotRequired[int]
    utp_fin_resends: NotRequired[int]
    utp_num_resends: NotRequired[int]
    utp_connect_timeout: NotRequired[int]
    utp_delayed_ack: NotRequired[int]
    utp_loss_multiplier: NotRequired[int]
    mixed_mode_algorithm: NotRequired[int]
    listen_queue_size: NotRequired[int]
    torrent_connect_boost: NotRequired[int]
    alert_queue_size: NotRequired[int]
    max_metadata_size: NotRequired[int]
    hashing_threads: NotRequired[int]
    checking_mem_usage: NotRequired[int]
    predictive_piece_announce: NotRequired[int]
    aio_threads: NotRequired[int]
    aio_max: NotRequired[int]
    network_threads: NotRequired[int]
    ssl_listen: NotRequired[int]
    tracker_backoff: NotRequired[int]
    share_ratio_limit: NotRequired[int]
    seed_time_ratio_limit: NotRequired[int]
    peer_turnover: NotRequired[int]
    peer_turnover_cutoff: NotRequired[int]
    peer_turnover_interval: NotRequired[int]
    connect_seed_every_n_download: NotRequired[int]
    max_http_recv_buffer_size: NotRequired[int]
    max_retry_port_bind: NotRequired[int]
    alert_mask: NotRequired[int]
    out_enc_policy: NotRequired[int]
    in_enc_policy: NotRequired[int]
    allowed_enc_level: NotRequired[int]
    inactive_down_rate: NotRequired[int]
    inactive_up_rate: NotRequired[int]
    proxy_type: NotRequired[int]
    proxy_port: NotRequired[int]
    i2p_port: NotRequired[int]
    cache_size_volatile: NotRequired[int]
    urlseed_max_request_bytes: NotRequired[int]
    web_seed_name_lookup_retry: NotRequired[int]
    close_file_interval: NotRequired[int]
    utp_cwnd_reduce_timer: NotRequired[int]
    max_web_seed_connections: NotRequired[int]
    resolver_cache_timeout: NotRequired[int]
    send_not_sent_low_watermark: NotRequired[int]
    rate_choker_initial_threshold: NotRequired[int]
    upnp_lease_duration: NotRequired[int]
    max_concurrent_http_announces: NotRequired[int]
    dht_max_peers_reply: NotRequired[int]
    dht_search_branching: NotRequired[int]
    dht_max_fail_count: NotRequired[int]
    dht_max_torrents: NotRequired[int]
    dht_max_dht_items: NotRequired[int]
    dht_max_peers: NotRequired[int]
    dht_max_torrent_search_reply: NotRequired[int]
    dht_block_timeout: NotRequired[int]
    dht_block_ratelimit: NotRequired[int]
    dht_item_lifetime: NotRequired[int]
    dht_sample_infohashes_interval: NotRequired[int]
    dht_max_infohashes_sample_count: NotRequired[int]
    max_piece_count: NotRequired[int]
    metadata_token_limit: NotRequired[int]
    disk_write_mode: NotRequired[int]
    mmap_file_size_cutoff: NotRequired[int]
    i2p_inbound_quantity: NotRequired[int]
    i2p_outbound_quantity: NotRequired[int]
    i2p_inbound_length: NotRequired[int]
    i2p_outbound_length: NotRequired[int]
    announce_port: NotRequired[int]
    i2p_inbound_length_variance: NotRequired[int]
    i2p_outbound_length_variance: NotRequired[int]
    allow_multiple_connections_per_ip: NotRequired[bool]
    ignore_limits_on_local_network: NotRequired[bool]
    send_redundant_have: NotRequired[bool]
    lazy_bitfields: NotRequired[bool]
    use_dht_as_fallback: NotRequired[bool]
    upnp_ignore_nonrouters: NotRequired[bool]
    use_parole_mode: NotRequired[bool]
    use_read_cache: NotRequired[bool]
    use_write_cache: NotRequired[bool]
    dont_flush_write_cache: NotRequired[bool]
    coalesce_reads: NotRequired[bool]
    coalesce_writes: NotRequired[bool]
    auto_manage_prefer_seeds: NotRequired[bool]
    dont_count_slow_torrents: NotRequired[bool]
    close_redundant_connections: NotRequired[bool]
    prioritize_partial_pieces: NotRequired[bool]
    rate_limit_ip_overhead: NotRequired[bool]
    announce_to_all_trackers: NotRequired[bool]
    announce_to_all_tiers: NotRequired[bool]
    prefer_udp_trackers: NotRequired[bool]
    strict_super_seeding: NotRequired[bool]
    lock_disk_cache: NotRequired[bool]
    disable_hash_checks: NotRequired[bool]
    allow_i2p_mixed: NotRequired[bool]
    low_prio_disk: NotRequired[bool]
    volatile_read_cache: NotRequired[bool]
    guided_read_cache: NotRequired[bool]
    no_atime_storage: NotRequired[bool]
    incoming_starts_queued_torrents: NotRequired[bool]
    report_true_downloaded: NotRequired[bool]
    strict_end_game_mode: NotRequired[bool]
    broadcast_lsd: NotRequired[bool]
    enable_outgoing_utp: NotRequired[bool]
    enable_incoming_utp: NotRequired[bool]
    enable_outgoing_tcp: NotRequired[bool]
    enable_incoming_tcp: NotRequired[bool]
    ignore_resume_timestamps: NotRequired[bool]
    no_recheck_incomplete_resume: NotRequired[bool]
    anonymous_mode: NotRequired[bool]
    report_web_seed_downloads: NotRequired[bool]
    rate_limit_utp: NotRequired[bool]
    announce_double_nat: NotRequired[bool]
    seeding_outgoing_connections: NotRequired[bool]
    no_connect_privileged_ports: NotRequired[bool]
    smooth_connects: NotRequired[bool]
    always_send_user_agent: NotRequired[bool]
    apply_ip_filter_to_trackers: NotRequired[bool]
    use_disk_read_ahead: NotRequired[bool]
    lock_files: NotRequired[bool]
    contiguous_recv_buffer: NotRequired[bool]
    ban_web_seeds: NotRequired[bool]
    allow_partial_disk_writes: NotRequired[bool]
    force_proxy: NotRequired[bool]
    support_share_mode: NotRequired[bool]
    support_merkle_torrents: NotRequired[bool]
    report_redundant_bytes: NotRequired[bool]
    listen_system_port_fallback: NotRequired[bool]
    use_disk_cache_pool: NotRequired[bool]
    announce_crypto_support: NotRequired[bool]
    enable_upnp: NotRequired[bool]
    enable_natpmp: NotRequired[bool]
    enable_lsd: NotRequired[bool]
    enable_dht: NotRequired[bool]
    prefer_rc4: NotRequired[bool]
    proxy_hostnames: NotRequired[bool]
    proxy_peer_connections: NotRequired[bool]
    auto_sequential: NotRequired[bool]
    proxy_tracker_connections: NotRequired[bool]
    enable_ip_notifier: NotRequired[bool]
    dht_prefer_verified_node_ids: NotRequired[bool]
    dht_restrict_routing_ips: NotRequired[bool]
    dht_restrict_search_ips: NotRequired[bool]
    dht_extended_routing_table: NotRequired[bool]
    dht_aggressive_lookups: NotRequired[bool]
    dht_privacy_lookups: NotRequired[bool]
    dht_enforce_node_id: NotRequired[bool]
    dht_ignore_dark_internet: NotRequired[bool]
    dht_read_only: NotRequired[bool]
    piece_extent_affinity: NotRequired[bool]
    validate_https_trackers: NotRequired[bool]
    ssrf_mitigation: NotRequired[bool]
    allow_idna: NotRequired[bool]
    enable_set_file_valid_data: NotRequired[bool]
    socks5_udp_send_local_ep: NotRequired[bool]
    proxy_send_host_in_connect: NotRequired[bool]


class session_params(metaclass=_BoostBaseClass):
    __instance_size__: int
    @overload
    def __init__(self) -> None: ...
    @overload
    def __init__(self, settings: settings_pack) -> None: ...
    dht_state: dht_state
    ip_filter: ip_filter
    settings: settings_pack


class session_stats_alert(alert):
    @property
    def values(self) -> dict[str, int]: ...


class session_stats_header_alert(alert):
    ...


@type_check_only
class UtpStatsdict(TypedDict):
    num_idle: int
    num_syn_sent: int
    num_connected: int
    num_fin_sent: int
    num_close_wait: int


class session_status(metaclass=_BoostBaseClass):
    __instance_size__: int
    @property
    def active_requests(self) -> list[dht_lookup]: ...
    @property
    def allowed_upload_slots(self) -> int: ...
    @property
    def dht_download_rate(self) -> int: ...
    @property
    def dht_global_nodes(self) -> int: ...
    @property
    def dht_node_cache(self) -> int: ...
    @property
    def dht_nodes(self) -> int: ...
    @property
    def dht_torrents(self) -> int: ...
    @property
    def dht_total_allocations(self) -> int: ...
    @property
    def dht_upload_rate(self) -> int: ...
    @property
    def down_bandwidth_bytes_queue(self) -> int: ...
    @property
    def down_bandwidth_queue(self) -> int: ...
    @property
    def download_rate(self) -> int: ...
    @property
    def has_incoming_connections(self) -> bool: ...
    @property
    def ip_overhead_download_rate(self) -> int: ...
    @property
    def ip_overhead_upload_rate(self) -> int: ...
    @property
    def num_peers(self) -> int: ...
    @property
    def num_unchoked(self) -> int: ...
    @property
    def optimistic_unchoke_counter(self) -> int: ...
    @property
    def payload_download_rate(self) -> int: ...
    @property
    def payload_upload_rate(self) -> int: ...
    @property
    def total_dht_download(self) -> int: ...
    @property
    def total_dht_upload(self) -> int: ...
    @property
    def total_download(self) -> int: ...
    @property
    def total_failed_bytes(self) -> int: ...
    @property
    def total_ip_overhead_download(self) -> int: ...
    @property
    def total_ip_overhead_upload(self) -> int: ...
    @property
    def total_payload_download(self) -> int: ...
    @property
    def total_payload_upload(self) -> int: ...
    @property
    def total_redundant_bytes(self) -> int: ...
    @property
    def total_tracker_download(self) -> int: ...
    @property
    def total_tracker_upload(self) -> int: ...
    @property
    def total_upload(self) -> int: ...
    @property
    def tracker_download_rate(self) -> int: ...
    @property
    def tracker_upload_rate(self) -> int: ...
    @property
    def unchoke_counter(self) -> int: ...
    @property
    def up_bandwidth_bytes_queue(self) -> int: ...
    @property
    def up_bandwidth_queue(self) -> int: ...
    @property
    def upload_rate(self) -> int: ...
    @property
    def utp_stats(self) -> UtpStatsdict: ...


class sha1_hash(metaclass=_BoostBaseClass):
    __instance_size__: int
    @overload
    def __init__(self) -> None: ...
    @overload
    def __init__(self, _digest: bytes | str) -> None: ...
    def clear(self) -> None: ...
    def is_all_zeros(self) -> bool: ...
    def to_bytes(self) -> bytes: ...
    def to_string(self) -> bytes: ...
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...
    def __lt__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...


class sha256_hash(metaclass=_BoostBaseClass):
    __instance_size__: int
    @overload
    def __init__(self) -> None: ...
    @overload
    def __init__(self, _digest: bytes | str) -> None: ...
    def clear(self) -> None: ...
    def is_all_zeros(self) -> bool: ...
    def to_bytes(self) -> bytes: ...
    def to_string(self) -> bytes: ...
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...
    def __lt__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...


class socket_type_t(int):
    http: int
    http_ssl: int
    i2p: int
    socks5: int
    socks5_ssl: int
    tcp: int
    tcp_ssl: int
    udp: int
    utp: int
    utp_ssl: int

    names: Final[dict[str, int]] = {
        "tcp": socket_type_t.tcp,  # noqa: F821
        "socks5": socket_type_t.socks5,  # noqa: F821
        "http": socket_type_t.http,  # noqa: F821
        "utp": socket_type_t.utp,  # noqa: F821
        "udp": socket_type_t.udp,  # noqa: F821
        "i2p": socket_type_t.i2p,  # noqa: F821
        "tcp_ssl": socket_type_t.tcp_ssl,  # noqa: F821
        "socks5_ssl": socket_type_t.socks5_ssl,  # noqa: F821
        "http_ssl": socket_type_t.http_ssl,  # noqa: F821
        "utp_ssl": socket_type_t.utp_ssl  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: socket_type_t.tcp,  # noqa: F821
        1: socket_type_t.socks5,  # noqa: F821
        2: socket_type_t.http,  # noqa: F821
        3: socket_type_t.udp,  # noqa: F821
        4: socket_type_t.i2p,  # noqa: F821
        5: socket_type_t.tcp_ssl,  # noqa: F821
        6: socket_type_t.socks5_ssl,  # noqa: F821
        7: socket_type_t.http_ssl,  # noqa: F821
        8: socket_type_t.utp_ssl  # noqa: F821
    }


class socks5_alert(alert):
    @property
    def error(self) -> error_code: ...
    @property
    def ip(self) -> tuple[str, int]: ...
    @property
    def op(self) -> operation_t: ...


class state_changed_alert(torrent_alert):
    @property
    def prev_state(self) -> torrent_status.states: ...
    @property
    def state(self) -> torrent_status.states: ...


class state_update_alert(alert):
    @property
    def status(self) -> list[torrent_status]: ...


class stats_alert(torrent_alert):
    @property
    def interval(self) -> int: ...
    @property
    def transferred(self) -> list[int]: ...


class stats_channel(int):
    download_dht_protocol: int
    download_ip_protocol: int
    download_payload: int
    download_protocol: int
    download_tracker_protocol: int
    upload_dht_protocol: int
    upload_ip_protocol: int
    upload_payload: int
    upload_protocol: int
    upload_tracker_protocol: int

    names: Final[dict[str, int]] = {
        "upload_payload": stats_channel.upload_payload,  # noqa: F821
        "upload_protocol": stats_channel.upload_protocol,  # noqa: F821
        "upload_ip_protocol": stats_channel.upload_ip_protocol,  # noqa: F821
        "upload_dht_protocol": stats_channel.upload_dht_protocol,  # noqa: F821
        "upload_tracker_protocol": stats_channel.upload_tracker_protocol,  # noqa: F821
        "download_payload": stats_channel.download_payload,  # noqa: F821
        "download_protocol": stats_channel.download_protocol,  # noqa: F821
        "download_ip_protocol": stats_channel.download_ip_protocol,  # noqa: F821
        "download_dht_protocol": stats_channel.download_dht_protocol,  # noqa: F821
        "download_tracker_protocol": stats_channel.download_tracker_protocol  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: stats_channel.upload_payload,  # noqa: F821
        1: stats_channel.upload_protocol,  # noqa: F821
        4: stats_channel.upload_ip_protocol,  # noqa: F821
        5: stats_channel.upload_dht_protocol,  # noqa: F821
        6: stats_channel.upload_tracker_protocol,  # noqa: F821
        2: stats_channel.download_payload,  # noqa: F821
        3: stats_channel.download_protocol,  # noqa: F821
        7: stats_channel.download_ip_protocol,  # noqa: F821
        8: stats_channel.download_dht_protocol,  # noqa: F821
        9: stats_channel.download_tracker_protocol  # noqa: F821
    }


class stats_metric(metaclass=_BoostBaseClass):
    __instance_size__: int
    @property
    def name(self) -> str: ...
    @property
    def type(self) -> metric_type_t: ...
    @property
    def value_index(self) -> int: ...


class status_flags_t(metaclass=_BoostBaseClass):
    __instance_size__: int
    query_accurate_download_counters: int
    query_distributed_copies: int
    query_last_seen_complete: int
    query_pieces: int
    query_verified_pieces: int


class storage_moved_alert(torrent_alert):
    def old_path(self) -> str: ...
    def storage_path(self) -> str: ...
    @property
    def path(self) -> str: ...


class storage_moved_failed_alert(torrent_alert):
    def file_path(self) -> str: ...
    @property
    def error(self) -> error_code: ...
    @property
    def op(self) -> operation_t: ...
    @property
    def operation(self) -> str: ...


class suggest_mode_t(int):
    no_piece_suggestions: int
    suggest_read_cache: int

    names: Final[dict[str, int]] = {
        "no_piece_suggestions": suggest_mode_t.no_piece_suggestions,  # noqa: F821
        "suggest_read_cache": suggest_mode_t.suggest_read_cache  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        0: suggest_mode_t.no_piece_suggestions,  # noqa: F821
        1: suggest_mode_t.suggest_read_cache  # noqa: F821
    }


class torrent_added_alert(torrent_alert):
    ...


class torrent_checked_alert(torrent_alert):
    ...


class torrent_conflict_alert(torrent_alert):
    @property
    def conflicting_torrent(self) -> torrent_handle: ...
    @property
    def metadata(self) -> torrent_info: ...


class torrent_delete_failed_alert(torrent_alert):
    @property
    def error(self) -> error_code: ...
    @property
    def info_hash(self) -> sha1_hash: ...
    @property
    def info_hashes(self) -> info_hash_t: ...
    @property
    def msg(self) -> str: ...


class torrent_deleted_alert(torrent_alert):
    @property
    def info_hash(self) -> sha1_hash: ...
    @property
    def info_hashes(self) -> info_hash_t: ...


class torrent_error_alert(torrent_alert):
    @property
    def error(self) -> error_code: ...


class torrent_finished_alert(torrent_alert):
    ...


class torrent_flags(metaclass=_BoostBaseClass):
    __instance_size__: int
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


@type_check_only
class BlockInfodict(TypedDict):
    state: int
    num_peers: int
    bytes_progress: int
    block_size: int
    peer: tuple[str, int]


@type_check_only
class PartialPieceInfodict(TypedDict):
    piece_index: int
    blocks_in_piece: int
    blocks: list[BlockInfodict]


@type_check_only
class ErrorCodedict(TypedDict):
    value: int
    category: str


@type_check_only
class AnnounceInfohashdict(TypedDict):
    message: str
    last_error: ErrorCodedict
    next_announce: datetime.datetime | None
    min_announce: datetime.datetime | None
    scrape_incomplete: int
    scrape_complete: int
    scrape_downloaded: int
    fails: int
    updating: bool
    start_sent: bool
    complete_sent: bool


@type_check_only
class AnnounceEndpointdict(TypedDict):
    local_address: tuple[str, int]
    info_hashes: list[AnnounceInfohashdict]
    message: str
    last_error: ErrorCodedict
    next_announce: datetime.datetime | None
    min_announce: datetime.datetime | None
    scrape_incomplete: int
    scrape_complete: int
    scrape_downloaded: int
    fails: int
    updating: bool
    start_sent: bool
    complete_sent: bool


@type_check_only
class AnnounceEntrydict(TypedDict):
    url: NotRequired[str]
    trackerid: NotRequired[str]
    tier: NotRequired[int]
    fail_limit: NotRequired[int]
    source: NotRequired[int]
    verified: NotRequired[bool]
    message: NotRequired[str]
    last_error: NotRequired[ErrorCodedict]
    next_announce: NotRequired[datetime.datetime]
    min_announce: NotRequired[datetime.datetime]
    scrape_incomplete: NotRequired[int]
    scrape_complete: NotRequired[int]
    scrape_downloaded: NotRequired[int]
    fails: NotRequired[int]
    updating: NotRequired[bool]
    start_sent: NotRequired[bool]
    complete_sent: NotRequired[bool]
    endpoints: NotRequired[list[AnnounceEndpointdict]]
    send_stats: NotRequired[bool]


class torrent_handle(metaclass=_BoostBaseClass):
    __instance_size__: int
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
    def add_tracker(self, _announce: AnnounceEntrydict) -> None: ...
    def add_url_seed(self, _url: str) -> None: ...
    def apply_ip_filter(self, _enabled: bool) -> None: ...
    def auto_managed(self, _enabled: bool) -> None: ...
    def clear_error(self) -> None: ...
    def clear_piece_deadlines(self) -> None: ...

    def connect_peer(
        self, _endpoint: tuple[str, int], source: int = ..., flags: int = ...
    ) -> None: ...
    def download_limit(self) -> int: ...
    def file_priorities(self) -> list[int]: ...
    @overload
    def file_priority(self, _prio: int) -> int: ...
    @overload
    def file_priority(self, _index: int, _prio: int) -> None: ...
    def file_progress(self, flags: int = ...) -> list[int]: ...
    def file_status(self) -> list[open_file_state]: ...
    def flags(self) -> int: ...
    def flush_cache(self) -> None: ...
    def force_dht_announce(self) -> None: ...
    def force_reannounce(self, seconds: int = 0, tracker_idx: int = -1, flags: int = 0) -> None: ...
    def force_recheck(self) -> None: ...
    def get_download_queue(self) -> list[PartialPieceInfodict]: ...
    def get_file_priorities(self) -> list[int]: ...
    def get_peer_info(self) -> list[peer_info]: ...
    def get_piece_priorities(self) -> list[int]: ...
    def get_torrent_info(self) -> torrent_info: ...
    def has_metadata(self) -> bool: ...
    def have_piece(self, _index: int) -> bool: ...
    def http_seeds(self) -> list[str]: ...
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
    def piece_availability(self) -> list[int]: ...
    def piece_priorities(self) -> list[int]: ...
    @overload
    def piece_priority(self, _index: int) -> int: ...
    @overload
    def piece_priority(self, _index: int, _prio: int) -> None: ...
    def post_download_queue(self) -> None: ...
    def post_file_progress(self, flags: int = 0) -> None: ...
    def post_peer_info(self) -> None: ...
    def post_piece_availability(self) -> None: ...
    def post_status(self, flags: int = 4294967295) -> None: ...
    def post_trackers(self) -> None: ...
    def prioritize_files(self, _priorities: list[int]) -> None: ...
    @overload
    def prioritize_pieces(self, _priorities: list[tuple[int, int]]) -> None: ...
    @overload
    def prioritize_pieces(self, _priorities: list[int]) -> None: ...
    def queue_position(self) -> int: ...
    def queue_position_bottom(self) -> None: ...
    def queue_position_down(self) -> None: ...
    def queue_position_top(self) -> None: ...
    def queue_position_up(self) -> None: ...
    def read_piece(self, _index: int) -> None: ...
    def remove_http_seed(self, _url: str) -> None: ...
    def remove_url_seed(self, _url: str) -> None: ...
    def rename_file(self, _index: int, _path: str) -> None: ...
    def replace_trackers(self, _entries: list[announce_entry]) -> None: ...
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
    def set_peer_download_limit(self, _endpoint: tuple[str, int], _limit: int) -> None: ...
    def set_peer_upload_limit(self, _endpoint: tuple[str, int], _limit: int) -> None: ...
    def set_piece_deadline(self, index: int, deadline: int, flags: int = ...) -> None: ...
    def set_priority(self, _prio: int) -> None: ...
    def set_ratio(self, _ratio: float) -> None: ...
    def set_sequential_download(self, _enabled: bool) -> None: ...
    def set_share_mode(self, _enabled: bool) -> None: ...
    def set_ssl_certificate(self, cert: str, private_key: str, dh_params: str, passphrase: str = "") -> None: ...
    def set_ssl_certificate_buffer(self, cert: str, private_key: str, dh_params: str) -> None: ...
    def set_tracker_login(self, _user: str, _pass: str) -> None: ...
    def set_upload_limit(self, _limit: int) -> None: ...
    def set_upload_mode(self, _enabled: bool) -> None: ...
    def status(self, flags: int = ...) -> torrent_status: ...
    def stop_when_ready(self, _enabled: bool) -> None: ...
    @overload
    def super_seeding(self, _enabled: bool) -> None: ...
    @overload
    def super_seeding(self) -> bool: ...
    def torrent_file(self) -> torrent_info | None: ...
    def trackers(self) -> list[AnnounceEntrydict]: ...
    def unset_flags(self, _flags: int) -> None: ...
    def upload_limit(self) -> int: ...
    def url_seeds(self) -> list[str]: ...
    def use_interface(self, _name: str) -> None: ...
    def write_resume_data(self) -> ResumeDataDict: ...
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...
    def __lt__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...


@type_check_only
class web_seed_entry(TypedDict):
    url: NotRequired[str]
    auth: NotRequired[str]
    type: NotRequired[int]


@type_check_only
class load_torrent_limits(dict):
    max_buffer_size: Literal[10000000]
    max_pieces: Literal[0x200000]
    max_decode_depth: Literal[100]
    max_decode_tokens: Literal[3000000]


class tracker_source(int):
    source_client: int
    source_magnet_link: int
    source_tex: int
    source_torrent: int

    names: Final[dict[str, int]] = {
        "source_torrent": tracker_source.source_torrent,  # noqa: F821
        "source_client": tracker_source.source_client,  # noqa: F821
        "source_magnet_link": tracker_source.source_magnet_link,  # noqa: F821
        "source_tex": tracker_source.source_tex  # noqa: F821
    }
    values: Final[dict[int, int]] = {
        1: tracker_source.source_torrent,  # noqa: F821
        2: tracker_source.source_client,  # noqa: F821
        4: tracker_source.source_magnet_link,  # noqa: F821
        8: tracker_source.source_tex  # noqa: F821
    }


class torrent_info(metaclass=_BoostBaseClass):
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
    def __init__(self, _path: str, _limits: load_torrent_limits) -> None: ...
    @overload
    def __init__(self, _bencoded: bytes) -> None: ...
    @overload
    def __init__(self, _bencoded: bytes, _limits: load_torrent_limits) -> None: ...
    @overload
    def __init__(self, _bdecoded: TorrentFileInfoDict) -> None: ...
    @overload
    def __init__(self, _bdecoded: TorrentFileInfoDictV2) -> None: ...
    @overload
    def __init__(self, _bdecoded: TorrentFileInfoDict, _limits: load_torrent_limits) -> None: ...
    @overload
    def __init__(self, _bdecoded: TorrentFileInfoDictV2, _limits: load_torrent_limits) -> None: ...
    def add_http_seed(self, url: str, extern_auth: str = "", extra_headers: list[tuple[str, str]] = []) -> None: ...
    def add_node(self, _ip: str, _port: int) -> None: ...
    def add_tracker(self, url: str, tier: int = 0, source: int = tracker_source.source_client) -> None: ...
    def add_url_seed(self, url: str, extern_auth: str = "", extra_headers: list[tuple[str, str]] = []) -> None: ...
    def collections(self) -> list[str]: ...
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
    def map_block(self, _piece: int, _offset: int, _size: int) -> list[file_slice]: ...
    def map_file(self, _file: int, _offset: int, _size: int) -> peer_request: ...
    def merkle_tree(self) -> list[bytes]: ...
    def metadata(self) -> bytes: ...
    def metadata_size(self) -> int: ...
    def name(self) -> str: ...
    def nodes(self) -> list[tuple[str, int]]: ...
    def num_files(self) -> int: ...
    def num_pieces(self) -> int: ...
    def orig_files(self) -> file_storage: ...
    def piece_length(self) -> int: ...
    def piece_size(self, _index: int) -> int: ...
    def priv(self) -> bool: ...
    def remap_files(self, _fs: file_storage) -> None: ...
    def rename_file(self, _index: int, _path: str) -> None: ...
    def set_merkle_tree(self, _tree: list[bytes]) -> None: ...
    def set_web_seeds(self, _seeds: list[web_seed_entry]) -> None: ...
    def similar_torrents(self) -> list[sha1_hash]: ...
    def ssl_cert(self) -> str: ...
    def total_size(self) -> int: ...
    def trackers(self) -> list[announce_entry]: ...
    def web_seeds(self) -> list[web_seed_entry]: ...


class torrent_log_alert(torrent_alert):
    def log_message(self) -> str: ...
    def msg(self) -> str: ...


class torrent_need_cert_alert(torrent_alert):
    @property
    def error(self) -> error_code: ...


class torrent_paused_alert(torrent_alert):
    ...


class torrent_removed_alert(torrent_alert):
    @property
    def info_hash(self) -> sha1_hash: ...
    @property
    def info_hashes(self) -> info_hash_t: ...


class torrent_resumed_alert(torrent_alert):
    ...


class torrent_status(metaclass=_BoostBaseClass):
    __instance_size__: int
    allocating: torrent_status.states  # noqa: F821
    checking_files: torrent_status.states  # noqa: F821
    checking_resume_data: torrent_status.states  # noqa: F821
    downloading: torrent_status.states  # noqa: F821
    downloading_metadata: torrent_status.states  # noqa: F821
    finished: torrent_status.states  # noqa: F821
    queued_for_checking: torrent_status.states  # noqa: F821
    seeding: torrent_status.states  # noqa: F821

    class states(int):
        allocating: int
        checking_files: int
        checking_resume_data: int
        downloading: int
        downloading_metadata: int
        finished: int
        queued_for_checking: int
        seeding: int

        names: Final[dict[str, int]] = {
            "queued_for_checking": torrent_status.states.queued_for_checking,  # noqa: F821
            "checking_files": torrent_status.states.checking_files,  # noqa: F821
            "downloading_metadata": torrent_status.states.downloading_metadata,  # noqa: F821
            "downloading": torrent_status.states.downloading,  # noqa: F821
            "finished": torrent_status.states.finished,  # noqa: F821
            "seeding": torrent_status.states.seeding,  # noqa: F821
            "allocating": torrent_status.states.allocating,  # noqa: F821
            "checking_resume_data": torrent_status.states.checking_resume_data  # noqa: F821
        }
        values: Final[dict[int, int]] = {
            0: torrent_status.states.queued_for_checking,  # noqa: F821
            1: torrent_status.states.checking_files,  # noqa: F821
            2: torrent_status.states.downloading_metadata,  # noqa: F821
            3: torrent_status.states.downloading,  # noqa: F821
            4: torrent_status.states.finished,  # noqa: F821
            5: torrent_status.states.seeding,  # noqa: F821
            6: torrent_status.states.allocating,  # noqa: F821
            7: torrent_status.states.checking_resume_data  # noqa: F821
        }

    def __eq__(self, other: object) -> bool: ...
    @property
    def active_duration(self) -> datetime.timedelta: ...
    @property
    def active_time(self) -> int: ...
    @property
    def added_time(self) -> int: ...
    @property
    def all_time_download(self) -> int: ...
    @property
    def all_time_upload(self) -> int: ...
    @property
    def announce_interval(self) -> datetime.timedelta: ...
    @property
    def announcing_to_dht(self) -> bool: ...
    @property
    def announcing_to_lsd(self) -> bool: ...
    @property
    def announcing_to_trackers(self) -> bool: ...
    @property
    def auto_managed(self) -> bool: ...
    @property
    def block_size(self) -> int: ...
    @property
    def completed_time(self) -> int: ...
    @property
    def connect_candidates(self) -> int: ...
    @property
    def connections_limit(self) -> int: ...
    @property
    def current_tracker(self) -> str: ...
    @property
    def distributed_copies(self) -> float: ...
    @property
    def distributed_fraction(self) -> int: ...
    @property
    def distributed_full_copies(self) -> int: ...
    @property
    def down_bandwidth_queue(self) -> int: ...
    @property
    def download_payload_rate(self) -> int: ...
    @property
    def download_rate(self) -> int: ...
    @property
    def errc(self) -> error_code: ...
    @property
    def error(self) -> str: ...
    @property
    def error_file(self) -> int: ...
    @property
    def finished_duration(self) -> datetime.timedelta: ...
    @property
    def finished_time(self) -> int: ...
    @property
    def flags(self) -> int: ...
    @property
    def handle(self) -> torrent_handle: ...
    @property
    def has_incoming(self) -> bool: ...
    @property
    def has_metadata(self) -> bool: ...
    @property
    def info_hash(self) -> sha1_hash: ...
    @property
    def info_hashes(self) -> info_hash_t: ...
    @property
    def ip_filter_applies(self) -> bool: ...
    @property
    def is_finished(self) -> bool: ...
    @property
    def is_loaded(self) -> bool: ...
    @property
    def is_seeding(self) -> bool: ...
    @property
    def last_download(self) -> datetime.datetime | None: ...
    @property
    def last_scrape(self) -> int: ...
    @property
    def last_seen_complete(self) -> int: ...
    @property
    def last_upload(self) -> datetime.datetime | None: ...
    @property
    def list_peers(self) -> int: ...
    @property
    def list_seeds(self) -> int: ...
    @property
    def moving_storage(self) -> bool: ...
    @property
    def name(self) -> str: ...
    @property
    def need_save_resume(self) -> bool: ...
    @property
    def next_announce(self) -> datetime.timedelta: ...
    @property
    def num_complete(self) -> int: ...
    @property
    def num_connections(self) -> int: ...
    @property
    def num_incomplete(self) -> int: ...
    @property
    def num_peers(self) -> int: ...
    @property
    def num_pieces(self) -> int: ...
    @property
    def num_seeds(self) -> int: ...
    @property
    def num_uploads(self) -> int: ...
    @property
    def paused(self) -> bool: ...
    @property
    def pieces(self) -> list[bool]: ...
    @property
    def priority(self) -> int: ...
    @property
    def progress(self) -> float: ...
    @property
    def progress_ppm(self) -> int: ...
    @property
    def queue_position(self) -> int: ...
    @property
    def save_path(self) -> str: ...
    @property
    def seed_mode(self) -> bool: ...
    @property
    def seed_rank(self) -> int: ...
    @property
    def seeding_duration(self) -> datetime.timedelta: ...
    @property
    def seeding_time(self) -> int: ...
    @property
    def sequential_download(self) -> bool: ...
    @property
    def share_mode(self) -> bool: ...
    @property
    def state(self) -> torrent_status.states: ...
    @property
    def stop_when_ready(self) -> bool: ...
    @property
    def storage_mode(self) -> storage_mode_t: ...
    @property
    def super_seeding(self) -> bool: ...
    @property
    def time_since_download(self) -> int: ...
    @property
    def time_since_upload(self) -> int: ...
    @property
    def torrent_file(self) -> torrent_info | None: ...
    @property
    def total(self) -> int: ...
    @property
    def total_done(self) -> int: ...
    @property
    def total_download(self) -> int: ...
    @property
    def total_failed_bytes(self) -> int: ...
    @property
    def total_payload_download(self) -> int: ...
    @property
    def total_payload_upload(self) -> int: ...
    @property
    def total_redundant_bytes(self) -> int: ...
    @property
    def total_upload(self) -> int: ...
    @property
    def total_wanted(self) -> int: ...
    @property
    def total_wanted_done(self) -> int: ...
    @property
    def up_bandwidth_queue(self) -> int: ...
    @property
    def upload_mode(self) -> bool: ...
    @property
    def upload_payload_rate(self) -> int: ...
    @property
    def upload_rate(self) -> int: ...
    @property
    def uploads_limit(self) -> int: ...
    @property
    def verified_pieces(self) -> list[bool]: ...


class tracker_announce_alert(tracker_alert):
    @property
    def event(self) -> event_t: ...
    @property
    def version(self) -> protocol_version: ...


class tracker_error_alert(tracker_alert):
    def error_message(self) -> str: ...
    def failure_reason(self) -> str: ...
    @property
    def error(self) -> error_code: ...
    @property
    def msg(self) -> str: ...
    @property
    def status_code(self) -> int: ...
    @property
    def times_in_row(self) -> int: ...
    @property
    def version(self) -> protocol_version: ...


class tracker_list_alert(tracker_alert):
    @property
    def trackers(self) -> list[announce_entry]: ...


class tracker_reply_alert(tracker_alert):
    @property
    def num_peers(self) -> int: ...
    @property
    def version(self) -> protocol_version: ...


class tracker_warning_alert(tracker_alert):
    @property
    def version(self) -> protocol_version: ...


class udp_error_alert(alert):
    @property
    def endpoint(self) -> tuple[str, int]: ...
    @property
    def error(self) -> error_code: ...


class unwanted_block_alert(peer_alert):
    @property
    def block_index(self) -> int: ...
    @property
    def piece_index(self) -> int: ...


class url_seed_alert(torrent_alert):
    def error_message(self) -> str: ...
    def server_url(self) -> str: ...
    @property
    def error(self) -> error_code: ...
    @property
    def msg(self) -> str: ...
    @property
    def url(self) -> str: ...


class write_flags(metaclass=_BoostBaseClass):
    __instance_size__: int
    allow_missing_piece_layer: int
    include_dht_nodes: int
    no_http_seeds: int
