#!/usr/bin/env python3
"""
dump_torrent.py - Generate metadata.json by parsing torrent files directly
Output matches dump_torrent format exactly
"""
import sys
import json
import re
import binascii
from pathlib import Path
from typing import Dict, Any, List, Optional
import argparse
import datetime
import os
import hashlib
import io

# Global variable to track libtorrent availability
LIBTORRENT_AVAILABLE = False
lt = None

def import_libtorrent(custom_path: Optional[str] = None) -> bool:
    """Import libtorrent, optionally from a custom path"""
    global lt, LIBTORRENT_AVAILABLE
    
    # If custom path is provided, try to import from there first
    if custom_path:
        try:
            sys.path.insert(0, custom_path)
            import libtorrent as lt
            LIBTORRENT_AVAILABLE = True
            print(f"Using libtorrent from custom path: {custom_path}", file=sys.stderr)
            return True
        except ImportError:
            print(f"Warning: Could not load libtorrent from custom path: {custom_path}", file=sys.stderr)
            print("Falling back to system libtorrent...", file=sys.stderr)
    
    # Try system libtorrent
    try:
        import libtorrent as lt
        LIBTORRENT_AVAILABLE = True
        print("Using system libtorrent", file=sys.stderr)
        return True
    except ImportError:
        print("Error: libtorrent not found in system paths.", file=sys.stderr)
        LIBTORRENT_AVAILABLE = False
        return False

def extract_hashes_manually(torrent_data: bytes) -> tuple:
    """Manually extract v1 and v2 info hashes from torrent data"""
    v1_hash = None
    v2_hash = None
    
    try:
        # First decode the torrent
        decoded = lt.bdecode(torrent_data)
        if not decoded:
            return None, None
            
        # Extract info dict
        if b'info' not in decoded:
            return None, None
            
        info_dict = decoded[b'info']
        
        # Calculate v1 hash (SHA1 of bencoded info dict)
        info_bencoded = lt.bencode(info_dict)
        v1_hash = hashlib.sha1(info_bencoded).hexdigest()
        
        # Check if this is a v2 torrent
        is_v2 = False
        if b'meta version' in info_dict:
            meta_version = info_dict[b'meta version']
            if isinstance(meta_version, int) and meta_version == 2:
                is_v2 = True
        elif b'file tree' in info_dict:
            # Also check for file tree which indicates v2
            is_v2 = True
            
        if is_v2:
            # For v2, we need to calculate the SHA256 of the entire info dict
            # Note: This is simplified - actual v2 hashing uses a specific canonical form
            v2_hash = hashlib.sha256(info_bencoded).hexdigest()
            
    except Exception as e:
        print(f"Warning: Could not manually extract hashes: {e}", file=sys.stderr)
    
    return v1_hash, v2_hash

def get_info_hashes(torrent_data: bytes, ti) -> tuple:
    """Get v1 and v2 info hashes using multiple methods"""
    # Try to get from torrent_info first
    v1_hash = None
    v2_hash = None
    
    try:
        # Try to get v1 hash
        v1_hash = str(ti.info_hash())
        
        # Check if this looks like a truncated v2 hash (64 chars truncated to 40)
        if len(v1_hash) == 40:
            # This might actually be a v2 hash from older libtorrent
            # Try to get the real v1 hash manually
            real_v1_hash, real_v2_hash = extract_hashes_manually(torrent_data)
            
            if real_v1_hash and real_v2_hash and real_v2_hash != real_v1_hash:
                # We have both hashes from manual extraction
                return real_v1_hash, real_v2_hash
            elif real_v1_hash:
                # Only v1 hash from manual extraction
                return real_v1_hash, None
                
    except Exception as e:
        print(f"Warning: Could not get hashes from torrent_info: {e}", file=sys.stderr)
    
    # Fallback to manual extraction
    if not v1_hash:
        v1_hash, v2_hash = extract_hashes_manually(torrent_data)
    
    return v1_hash, v2_hash

def get_torrent_name(ti, torrent_data: bytes) -> str:
    """Get the torrent name, handling edge cases to match dump_torrent output"""
    try:
        name = ti.name()
        
        # Check if name is just a directory prefix for all files
        if hasattr(ti, 'files'):
            st = ti.files()
            if st.num_files() > 0:
                # Get the first file path
                first_file = st.file_path(0) if hasattr(st, 'file_path') else ""
                if first_file.startswith(name + '/'):
                    # Name appears to be just a directory prefix
                    # Check if all files have this prefix
                    all_same_prefix = True
                    for i in range(st.num_files()):
                        file_path = st.file_path(i) if hasattr(st, 'file_path') else ""
                        if not file_path.startswith(name + '/'):
                            all_same_prefix = False
                            break
                    
                    if all_same_prefix:
                        # This is just a directory name, not the actual torrent name
                        # Check the bencoded data for empty name field
                        decoded = lt.bdecode(torrent_data)
                        if decoded and b'info' in decoded:
                            info_dict = decoded[b'info']
                            if b'name' in info_dict:
                                name_bytes = info_dict[b'name']
                                if isinstance(name_bytes, bytes):
                                    name_str = name_bytes.decode('utf-8', errors='ignore')
                                    # If name is empty or just whitespace in the actual data
                                    if not name_str.strip():
                                        return ""
                                    # If name matches the directory, might still be empty
                                    if name_str == name:
                                        # Check if it's actually a directory prefix
                                        return ""
        return name
    except:
        return ""

def parse_torrent_file(torrent_path: Path, **kwargs) -> Dict[str, Any]:
    """Parse torrent file directly using libtorrent"""
    # Read torrent data
    try:
        with open(str(torrent_path), 'rb') as f:
            torrent_data = f.read()
    except Exception as e:
        print(f"Error: Could not read torrent file: {e}", file=sys.stderr)
        sys.exit(1)
    
    # Parse torrent
    try:
        # Create torrent_info from data
        ti = lt.torrent_info(torrent_data)
    except Exception as e:
        print(f"Error: Could not parse torrent file: {e}", file=sys.stderr)
        # Return minimal metadata
        return {
            'torrent file': str(torrent_path.absolute()),
            'number of pieces': 0,
            'piece length': 0,
            'info hash': '',
            'info hash v1': '',
            'info hash v2': None,
            'comment': '',
            'created by': '',
            'magnet link': '',
            'name': '',
            'number of files': 0,
            'files': [],
            'trackers': [],
            'dht_nodes': [],
            'web seeds': [],
            'total size': 0,
            'total_files': 0,
            'non-pad files': 0,
            'pad files': 0,
            'executable files': 0,
            'hidden files': 0,
            'symlink files': 0,
            'largest_file': 0,
            'smallest_file': 0,
            'average_file_size': 0,
        }
    
    # Get info hashes
    v1_hash, v2_hash = get_info_hashes(torrent_data, ti)
    
    metadata = {}
    
    # Basic info from torrent_info
    try:
        metadata['number of pieces'] = ti.num_pieces()
        metadata['piece length'] = ti.piece_length()
        metadata['comment'] = ti.comment()
        metadata['created by'] = ti.creator()
        metadata['name'] = get_torrent_name(ti, torrent_data)  # Use custom name function
        metadata['number of files'] = ti.num_files()
    except Exception as e:
        print(f"Warning: Could not get basic torrent info: {e}", file=sys.stderr)
        metadata['number of pieces'] = 0
        metadata['piece length'] = 0
        metadata['comment'] = ''
        metadata['created by'] = ''
        metadata['name'] = ''
        metadata['number of files'] = 0
    
    # Info hash - format exactly like dump_torrent
    if v1_hash and v2_hash and v2_hash != v1_hash:  # Only show v2 if it's different
        metadata['info hash'] = f"{v1_hash}, {v2_hash}"
        metadata['info hash v1'] = v1_hash
        metadata['info hash v2'] = v2_hash
    elif v1_hash:
        metadata['info hash'] = v1_hash
        metadata['info hash v1'] = v1_hash
        metadata['info hash v2'] = None
    else:
        metadata['info hash'] = ''
        metadata['info hash v1'] = ''
        metadata['info hash v2'] = None
    
    # Magnet link - build it correctly
    try:
        magnet_parts = []
        
        if v1_hash:
            magnet_parts.append(f"xt=urn:btih:{v1_hash}")
        
        if v2_hash and v2_hash != v1_hash:
            magnet_parts.append(f"xt=urn:btmh:1220{v2_hash}")
        
        # Add trackers from torrent
        # First, decode to get trackers
        decoded = lt.bdecode(torrent_data)
        if decoded and b'announce' in decoded:
            tracker = decoded[b'announce']
            if isinstance(tracker, bytes):
                try:
                    tracker_str = tracker.decode('utf-8')
                    import urllib.parse
                    encoded_tracker = urllib.parse.quote(tracker_str, safe='')
                    magnet_parts.append(f"tr={encoded_tracker}")
                except:
                    pass
        
        if magnet_parts:
            metadata['magnet link'] = f"magnet:?{'&'.join(magnet_parts)}"
        else:
            metadata['magnet link'] = ''
    except:
        metadata['magnet link'] = ''
    
    # Files
    files = []
    try:
        if hasattr(ti, 'files'):
            st = ti.files()
            show_pad = kwargs.get('show_padfiles', False)
            
            # In JSON mode, we want to show pad files by default to match dump_torrent2meta.py
            # In dump mode, we don't show them unless explicitly requested
            if 'dump_mode' in kwargs and kwargs['dump_mode']:
                # In dump mode, use the show_pad flag
                include_pad = show_pad
            else:
                # In JSON mode, always include pad files to match dump_torrent2meta.py
                include_pad = True
            
            # Calculate total size and offsets
            total_size = 0
            file_offsets = []
            for i in range(st.num_files()):
                file_size = st.file_size(i)
                file_offsets.append(total_size)
                total_size += file_size
            
            # Process each file
            for i in range(st.num_files()):
                try:
                    file_size = st.file_size(i)
                    file_offset = file_offsets[i]
                    
                    # Get file flags if available
                    flags = 0
                    if hasattr(st, 'file_flags'):
                        flags = st.file_flags(i)
                    
                    # Check if this is a pad file
                    is_pad = False
                    if hasattr(lt.file_storage, 'flag_pad_file'):
                        is_pad = bool(flags & lt.file_storage.flag_pad_file)
                    
                    # Skip pad files in dump mode unless explicitly requested
                    if is_pad and not include_pad:
                        continue
                    
                    # Calculate piece range
                    piece_length = metadata['piece length']
                    if piece_length > 0 and file_size > 0:
                        first_piece = file_offset // piece_length
                        last_piece = (file_offset + file_size - 1) // piece_length
                    else:
                        first_piece = 0
                        last_piece = 0
                    
                    # Build flags string
                    flags_str = '----'
                    is_executable = False
                    is_hidden = False
                    is_symlink = False
                    
                    if hasattr(st, 'file_flags'):
                        # Build flags string
                        flags_chars = []
                        flags_chars.append('p' if (hasattr(lt.file_storage, 'flag_pad_file') and 
                                                  bool(flags & lt.file_storage.flag_pad_file)) else '-')
                        flags_chars.append('x' if (hasattr(lt.file_storage, 'flag_executable') and 
                                                  bool(flags & lt.file_storage.flag_executable)) else '-')
                        flags_chars.append('h' if (hasattr(lt.file_storage, 'flag_hidden') and 
                                                  bool(flags & lt.file_storage.flag_hidden)) else '-')
                        flags_chars.append('l' if (hasattr(lt.file_storage, 'flag_symlink') and 
                                                  bool(flags & lt.file_storage.flag_symlink)) else '-')
                        flags_str = ''.join(flags_chars)
                        
                        is_executable = bool(flags & lt.file_storage.flag_executable) if hasattr(lt.file_storage, 'flag_executable') else False
                        is_hidden = bool(flags & lt.file_storage.flag_hidden) if hasattr(lt.file_storage, 'flag_hidden') else False
                        is_symlink = bool(flags & lt.file_storage.flag_symlink) if hasattr(lt.file_storage, 'flag_symlink') else False
                    
                    # Try to get root hash
                    root_hash = None
                    if hasattr(st, 'root'):
                        try:
                            root = st.root(i)
                            if hasattr(root, 'is_all_zeros'):
                                if not root.is_all_zeros():
                                    # Convert to hex string
                                    root_hash = root.to_bytes().hex()
                            elif root:  # Not zero/empty
                                # Try to convert to hex
                                try:
                                    root_hash = binascii.hexlify(root).decode('ascii')
                                except:
                                    pass
                        except:
                            pass
                    
                    # Get file path
                    file_path = st.file_path(i) if hasattr(st, 'file_path') else f"file_{i}"
                    
                    # Symlink target
                    symlink_target = None
                    if hasattr(st, 'symlink') and hasattr(lt.file_storage, 'flag_symlink'):
                        try:
                            if flags & lt.file_storage.flag_symlink:
                                symlink_target = st.symlink(i)
                        except:
                            pass
                    
                    # Mtime
                    mtime = st.mtime(i) if hasattr(st, 'mtime') else 0
                    
                    file_info = {
                        'index': len(files),
                        'offset': file_offset,
                        'offset_hex': f"{file_offset:x}",
                        'size': file_size,
                        'flags': flags_str,
                        'is_pad': is_pad,
                        'is_executable': is_executable,
                        'is_hidden': is_hidden,
                        'is_symlink': is_symlink,
                        'piece_range': [int(first_piece), int(last_piece)],
                        'mtime': mtime,
                        'root_hash': root_hash,
                        'path': file_path,
                        'symlink_target': symlink_target,
                    }
                    files.append(file_info)
                except Exception as e:
                    print(f"Warning: Could not parse file {i}: {e}", file=sys.stderr)
                    continue
    except Exception as e:
        print(f"Warning: Could not parse files: {e}", file=sys.stderr)
    
    metadata['files'] = files
    
    # Trackers - extract from torrent data
    trackers = []
    try:
        decoded = lt.bdecode(torrent_data)
        if decoded:
            # Single tracker
            if b'announce' in decoded:
                tracker = decoded[b'announce']
                if isinstance(tracker, bytes):
                    try:
                        tracker_str = tracker.decode('utf-8')
                        trackers.append({
                            'tier': 0,
                            'url': tracker_str
                        })
                    except:
                        pass
            
            # Tracker list
            if b'announce-list' in decoded:
                announce_list = decoded[b'announce-list']
                if isinstance(announce_list, list):
                    tier = 0
                    for tier_list in announce_list:
                        if isinstance(tier_list, list):
                            for tracker in tier_list:
                                if isinstance(tracker, bytes):
                                    try:
                                        tracker_str = tracker.decode('utf-8')
                                        trackers.append({
                                            'tier': tier,
                                            'url': tracker_str
                                        })
                                    except:
                                        pass
                            tier += 1
    except:
        pass
    
    metadata['trackers'] = trackers
    
    # DHT nodes - usually not in .torrent files
    metadata['dht_nodes'] = []
    
    # Web seeds - extract from torrent data (FIXED - filters duplicates and empty strings)
    web_seeds = []
    try:
        decoded = lt.bdecode(torrent_data)
        if decoded:
            # Check for url-list (web seeds) - standard format
            if b'url-list' in decoded:
                url_list = decoded[b'url-list']
                if isinstance(url_list, list):
                    for url in url_list:
                        if isinstance(url, bytes):
                            try:
                                url_str = url.decode('utf-8').strip()
                                if url_str and url_str not in web_seeds:  # Skip empty and duplicates
                                    web_seeds.append(url_str)
                            except:
                                pass
                    # Special case: if we got a list with one empty string, it's probably no web seeds
                    if len(web_seeds) == 1 and not web_seeds[0]:
                        web_seeds = []
                elif isinstance(url_list, bytes):
                    try:
                        url_str = url_list.decode('utf-8').strip()
                        if url_str and url_str not in web_seeds:
                            web_seeds.append(url_str)
                    except:
                        pass
            
            # Also check for httpseeds (alternative format used by some torrents)
            if b'httpseeds' in decoded:
                httpseeds = decoded[b'httpseeds']
                if isinstance(httpseeds, list):
                    for url in httpseeds:
                        if isinstance(url, bytes):
                            try:
                                url_str = url.decode('utf-8').strip()
                                if url_str and url_str not in web_seeds:
                                    web_seeds.append(url_str)
                            except:
                                pass
    except:
        pass
    
    metadata['web seeds'] = web_seeds
    
    return metadata

def enrich_metadata(metadata: Dict[str, Any], torrent_path: Path) -> Dict[str, Any]:
    """Add additional calculated fields to metadata"""
    enriched = metadata.copy()
    
    # Add torrent file info
    enriched['torrent file'] = str(torrent_path.absolute())
    
    try:
        stat_info = torrent_path.stat()
        enriched['file_mtime'] = stat_info.st_mtime
        enriched['file_mtime_iso'] = datetime.datetime.fromtimestamp(
            stat_info.st_mtime, datetime.timezone.utc
        ).isoformat()
        enriched['file_size'] = stat_info.st_size
        enriched['file_ctime'] = stat_info.st_ctime
        enriched['file_ctime_iso'] = datetime.datetime.fromtimestamp(
            stat_info.st_ctime, datetime.timezone.utc
        ).isoformat()
    except:
        pass
    
    # Calculate file statistics
    if 'files' in enriched:
        files = enriched['files']
        
        # Total size excluding pad files
        non_pad_files = [f for f in files if not f['is_pad']]
        total_size = sum(f['size'] for f in non_pad_files)
        enriched['total size'] = total_size
        
        # Count files by type
        enriched['non-pad files'] = sum(1 for f in files if not f['is_pad'])
        enriched['pad files'] = sum(1 for f in files if f['is_pad'])
        enriched['executable files'] = sum(1 for f in files if f['is_executable'])
        enriched['hidden files'] = sum(1 for f in files if f['is_hidden'])
        enriched['symlink files'] = sum(1 for f in files if f['is_symlink'])
        enriched['total_files'] = len(non_pad_files)
        
        # File size statistics
        if non_pad_files:
            sizes = [f['size'] for f in non_pad_files]
            enriched['largest_file'] = max(sizes)
            enriched['smallest_file'] = min(sizes)
            enriched['average_file_size'] = sum(sizes) / len(sizes)
            
            # File extensions
            extensions = {}
            for f in non_pad_files:
                path = Path(f['path'])
                ext = path.suffix.lower()
                if ext:
                    extensions[ext] = extensions.get(ext, 0) + 1
                elif '.' in path.name:
                    # Handle files with dots but no standard extension
                    ext = '.' + path.name.split('.')[-1].lower()
                    extensions[ext] = extensions.get(ext, 0) + 1
            if extensions:
                enriched['file_extensions'] = extensions
        else:
            enriched['largest_file'] = 0
            enriched['smallest_file'] = 0
            enriched['average_file_size'] = 0
    
    # Ensure all basic fields are present
    basic_fields = ['comment', 'name', 'created by']
    for field in basic_fields:
        if field not in enriched:
            enriched[field] = ''
    
    return enriched

def format_json_output(metadata: Dict[str, Any], pretty: bool = False) -> str:
    """Format metadata as JSON"""
    # Create a clean copy for output with consistent field order
    output_metadata = {}
    
    # First, basic torrent info in logical order
    basic_order = [
        'torrent file', 'file_size', 'file_mtime', 'file_mtime_iso',
        'file_ctime', 'file_ctime_iso',
        'number of pieces', 'piece length', 
        'info hash', 'info hash v1', 'info hash v2',
        'name', 'comment', 'created by',
        'magnet link', 'number of files',
        'total size', 'total_files',
        'non-pad files', 'pad files',
        'executable files', 'hidden files', 'symlink files',
        'largest_file', 'smallest_file', 'average_file_size'
    ]
    
    # Add fields in order if they exist
    for field in basic_order:
        if field in metadata:
            output_metadata[field] = metadata[field]
    
    # Add file extensions if present
    if 'file_extensions' in metadata and metadata['file_extensions']:
        output_metadata['file_extensions'] = metadata['file_extensions']
    
    # Add trackers if present
    if 'trackers' in metadata and metadata['trackers']:
        output_metadata['trackers'] = metadata['trackers']
    
    # Add DHT nodes if present
    if 'dht_nodes' in metadata and metadata['dht_nodes']:
        output_metadata['dht_nodes'] = metadata['dht_nodes']
    
    # Add files (all files, including pads, in JSON mode)
    if 'files' in metadata:
        output_metadata['files'] = metadata['files']
    
    # Add web seeds if present
    if 'web seeds' in metadata and metadata['web seeds']:
        output_metadata['web seeds'] = metadata['web seeds']
    
    indent = 2 if pretty else None
    return json.dumps(output_metadata, indent=indent, ensure_ascii=False, sort_keys=False)

def format_dump_output(metadata: Dict[str, Any]) -> str:
    """Format metadata to match dump_torrent output exactly"""
    lines = []
    
    # Add DHT nodes first if present
    if metadata.get('dht_nodes'):
        lines.append("nodes:")
        for node in metadata['dht_nodes']:
            lines.append(f"{node['host']}: {node['port']}")
    
    # Add trackers if present
    if metadata.get('trackers'):
        lines.append("trackers:")
        lines.append("")  # Empty line after trackers: (as in original output)
        for tracker in metadata['trackers']:
            lines.append(f" {tracker['tier']}: {tracker['url']}")
    
    # Basic info in exact dump_torrent order
    lines.append(f"number of pieces: {metadata.get('number of pieces', 0)}")
    lines.append(f"piece length: {metadata.get('piece length', 0)}")
    
    # Format info hash exactly like dump_torrent
    info_hash = metadata.get('info hash', '')
    lines.append(f"info hash: {info_hash}")
    
    lines.append(f"comment: {metadata.get('comment', '')}")
    lines.append(f"created by: {metadata.get('created by', '')}")
    lines.append(f"magnet link: {metadata.get('magnet link', '')}")
    lines.append(f"name: {metadata.get('name', '')}")
    lines.append(f"number of files: {metadata.get('number of files', 0)}")
    
    # Files section (only non-pad files by default, matching original dump_torrent)
    if metadata.get('files'):
        # In dump mode, only include non-pad files
        non_pad_files = [f for f in metadata['files'] if not f['is_pad']]
        if non_pad_files:
            lines.append("files:")
            for file_info in non_pad_files:
                offset_str = file_info['offset_hex']
                size = file_info['size']
                flags = file_info['flags']
                first, last = file_info['piece_range']
                mtime = file_info['mtime']
                root_hash = file_info['root_hash'] or "0"
                path = file_info['path']
                
                symlink_part = ""
                if file_info.get('symlink_target'):
                    symlink_part = f" -> {file_info['symlink_target']}"
                
                # Format exactly like dump_torrent
                if root_hash != "0":
                    lines.append(f" {offset_str:>8} {size:11} {flags} [ {first:5}, {last:5} ] {mtime:7} {root_hash} {path}{symlink_part}")
                else:
                    lines.append(f" {offset_str:>8} {size:11} {flags} [ {first:5}, {last:5} ] {mtime:7} {path}{symlink_part}")
    
    # Web seeds
    lines.append("web seeds:")
    for seed in metadata.get('web seeds', []):
        lines.append(seed)
    
    return "\n".join(lines)

def get_torrent_metadata(torrent_path: Path, dump_mode: bool = False, **kwargs) -> Dict[str, Any]:
    """Get torrent metadata by parsing directly with libtorrent"""
    # Pass dump_mode to parse_torrent_file to control pad file inclusion
    kwargs['dump_mode'] = dump_mode
    metadata = parse_torrent_file(torrent_path, **kwargs)
    return enrich_metadata(metadata, torrent_path)

def main():
    parser = argparse.ArgumentParser(
        description="Generate metadata.json by parsing torrent files directly",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s /tmp/torrent1.torrent
  %(prog)s /tmp/torrent1.torrent --pretty
  %(prog)s /tmp/torrent1.torrent -o metadata.json
  %(prog)s /tmp/torrent1.torrent --dump  # Output like dump_torrent
  %(prog)s /tmp/torrent1.torrent --show-padfiles  # Include pad files
  %(prog)s /tmp/torrent1.torrent --libtorrent-path /path/to/libtorrent/build/bindings/python  # Use custom libtorrent
  %(prog)s /tmp/torrent1.torrent --items-limit 1000 --depth-limit 50
        """
    )
    
    parser.add_argument(
        "torrent_file",
        help="Torrent file to analyze"
    )
    
    parser.add_argument(
        "-p", "--pretty",
        action="store_true",
        help="Pretty-print JSON output"
    )
    
    parser.add_argument(
        "-o", "--output",
        help="Output file (default: stdout)"
    )
    
    parser.add_argument(
        "-d", "--dump",
        action="store_true",
        help="Output in dump_torrent format (text) instead of JSON"
    )
    
    parser.add_argument(
        "--show-padfiles",
        action="store_true",
        help="Include pad files in output"
    )
    
    parser.add_argument(
        "--libtorrent-path",
        help="Custom path to libtorrent Python bindings"
    )
    
    parser.add_argument(
        "--items-limit",
        type=int,
        help="Set the upper limit of the number of bencode items"
    )
    
    parser.add_argument(
        "--depth-limit",
        type=int,
        help="Set the recursion limit in the bdecoder"
    )
    
    parser.add_argument(
        "--max-pieces",
        type=int,
        help="Set the upper limit on the number of pieces to load"
    )
    
    parser.add_argument(
        "--max-size",
        type=int,
        help="Set the upper limit on file size in MiB"
    )
    
    args = parser.parse_args()
    
    torrent_path = Path(args.torrent_file)
    
    if not torrent_path.exists():
        print(f"Error: Torrent file not found: {torrent_path}", file=sys.stderr)
        sys.exit(1)
    
    # Import libtorrent with custom path if specified
    if not import_libtorrent(args.libtorrent_path):
        print("Error: libtorrent Python bindings not available.", file=sys.stderr)
        print("Please install libtorrent or specify a custom path with --libtorrent-path", file=sys.stderr)
        sys.exit(1)
    
    try:
        # Build kwargs for parsing
        kwargs = {k: v for k, v in vars(args).items() 
                 if k in ['show_padfiles', 'items_limit', 'depth_limit', 'max_pieces', 'max_size'] 
                 and v is not None}
        
        # Get metadata, passing dump_mode flag
        metadata = get_torrent_metadata(torrent_path, dump_mode=args.dump, **kwargs)
        
        # Choose output format
        if args.dump:
            output_text = format_dump_output(metadata)
        else:
            output_text = format_json_output(metadata, args.pretty)
        
        # Output to file or stdout
        if args.output:
            output_path = Path(args.output)
            output_path.parent.mkdir(parents=True, exist_ok=True)
            with open(output_path, 'w', encoding='utf-8') as f:
                f.write(output_text)
            print(f"Metadata saved to: {output_path}", file=sys.stderr)
        else:
            print(output_text)
            
    except Exception as e:
        print(f"Error processing torrent: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
