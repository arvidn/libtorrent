# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import lldb
import struct

# this is an LLDB pretty printer for libtorrent types. To use in LLDB run:
#
#   command script import tools/libtorrent_lldb.py
#
# or add it to your ~/.lldbinit

def __lldb_init_module (debugger, dict):
    debugger.HandleCommand("type summary add -x \"^libtorrent::digest32<.+>$\" -F " +
        "libtorrent_lldb.print_hash -p -w libtorrent")
    debugger.HandleCommand("type summary add -x \"^libtorrent::sha256_hash$\" -F " +
        "libtorrent_lldb.print_hash -p -w libtorrent")
    debugger.HandleCommand("type summary add -x \"^libtorrent::sha1_hash$\" -F " +
        "libtorrent_lldb.print_hash -p -w libtorrent")
    debugger.HandleCommand("type summary add -x \"^libtorrent::span<.+>$\" -F " +
        "libtorrent_lldb.print_span -p -w libtorrent")
    debugger.HandleCommand("type summary add -x \"^libtorrent::flags::bitfield_flag<.+>$\" -F " +
        "libtorrent_lldb.print_flag -p -w libtorrent")
    debugger.HandleCommand("type summary add -x \"^boost::asio::ip::basic_endpoint<.+>$\" -F " +
        "libtorrent_lldb.print_endpoint -p -w libtorrent")
    debugger.HandleCommand("type summary add -x \"^libtorrent::bitfield$\" -F " +
        "libtorrent_lldb.print_bitfield -p -w libtorrent")
    debugger.HandleCommand("type summary add -x \"^libtorrent::typed_bitfield<.+>$\" -F " +
        "libtorrent_lldb.print_bitfield -p -w libtorrent")
    debugger.HandleCommand("type summary add -x \"^libtorrent::aux::strong_typedef<.+>$\" -F " +
        "libtorrent_lldb.print_strong_type -p -w libtorrent")
    debugger.HandleCommand("type category enable libtorrent")

def print_hash(valobj, internal_dict):

    if valobj.GetType().IsReferenceType():
        valobj = valobj.Dereference()

    data = valobj.GetChildMemberWithName("m_number").GetData().uint8s
    return bytes(data).hex()

def print_flag(valobj, internal_dict):

    if valobj.GetType().IsReferenceType():
        valobj = valobj.Dereference()

    data = valobj.GetChildMemberWithName("m_val").GetValueAsUnsigned()
    return "({}) {:b}".format(valobj.GetType().name, data)

def swap16(i):
    return struct.unpack("<H", struct.pack(">H", i))[0]

def pairs(lst):
    for i in range(0, len(lst), 2):
        yield lst[i:i+2]

def print_endpoint(valobj, internal_dict):

    if valobj.GetType().IsReferenceType():
        valobj = valobj.Dereference()

    union = valobj.GetChildMemberWithName("impl_").GetChildMemberWithName("data_")
    family = union.GetChildMemberWithName("base").GetChildMemberWithName("sa_family").GetValueAsUnsigned()

    if family == 2:
        a = union.GetChildMemberWithName("v4").GetChildMemberWithName("sin_addr").GetData().uint8s
        addr = ".".join([f"{b}" for b in a ])
        p = swap16(union.GetChildMemberWithName("v4").GetChildMemberWithName("sin_port").GetValueAsUnsigned())
        return "{}:{}".format(addr, p)
    else:
        a = union.GetChildMemberWithName("v6").GetChildMemberWithName("sin6_addr").GetData().uint8s
        p = swap16(union.GetChildMemberWithName("v6").GetChildMemberWithName("sin6_port").GetValueAsUnsigned())
        addr = ":".join(x+y for x, y in pairs(["{:02x}".format(b) for b in a]))
        return "[{}]:{}".format(addr, p)

def print_bitfield(valobj, internal_dict):

    if valobj.GetType().IsReferenceType():
        valobj = valobj.Dereference()

    array = valobj.GetChildMemberWithName("m_buf").GetChildMemberWithName("__ptr_").GetChildMemberWithName("__value_")
    size = array.Dereference().GetValueAsUnsigned()
    ret = "size: {} bits | ".format(size)
    for idx in range((size + 31) // 32):
        item = array.GetChildAtIndex(idx + 1, lldb.eNoDynamicValues, True)
        buffer = item.GetData().uint8s
        for b in buffer:
            ret += "{:08b}".format(int(b))
            size -= 8

    return ret

def print_span(valobj, internal_dict):

    if valobj.GetType().IsReferenceType():
        valobj = valobj.Dereference()

    array = valobj.GetChildMemberWithName("m_ptr")
    size = valobj.GetChildMemberWithName("m_len").GetValueAsSigned()
    ret = "size = {}".format(size)
    for idx in range(size):
        if idx == 0:
            item = array.Dereference()
        else:
            item = array.GetChildAtIndex(idx, lldb.eNoDynamicValues, True)
        ret += "\n[{}] = {}".format(idx, item.summary)
    return ret


def print_strong_type(valobj, internal_dict):

    if valobj.GetType().IsReferenceType():
        valobj = valobj.Dereference()

    name = valobj.GetType().name
    data = valobj.GetChildMemberWithName("m_val").GetValue()
    if "piece_index_tag" in name:
        name = "piece_index"
    elif "file_index_tag" in name:
        name = "file_index"
    elif "queue_position_tag" in name:
        name = "queue_pos"
    elif "piece_extent_tag" in name:
        name = "piece_extent"
    elif "storage_index_tag_t" in name:
        name = "storage_index"
    elif "disconnect_severity_tag" in name:
        name = "disconnect_severity"
        val = valobj.GetChildMemberWithName("m_val").GetValueAsUnsigned()
        if val == 0:
            data = "normal"
        elif val == 1:
            data = "failure"
        elif val == 2:
            data = "peer_error"
        else:
            data = "<unknown> ({})".format(val)
    elif "prio_index_tag_t" in name:
        name = "prio_index"
    elif "port_mapping_tag" in name:
        name = "port_mapping"
    elif "dl_queue_tag" in name or name == "libtorrent::download_queue_t":
        name = "download_queue"
        val = valobj.GetChildMemberWithName("m_val").GetValueAsUnsigned()
        if val == 0:
            data = "piece_downloading";
        elif val == 1:
            data = "piece_full";
        elif val == 2:
            data = "piece_finished";
        elif val == 3:
            data = "piece_zero_prio";
        elif val == 4:
            data = "piece_open";
        elif val == 5:
            data = "piece_downloading_reverse";
        elif val == 6:
            data = "piece_full_reverse";
        else:
            data = "<unknown> ({})".format(val)
    elif "piece_extent_tag" in name:
        name = "piece_extent"
    elif "picker_options_tag" in name:
        name = "picker_options"
        val = valobj.GetChildMemberWithName("m_val").GetValueAsUnsigned()
        flags = []
        if (val & 1) != 0:
            flags.append("rarest_first")
        if (val & 2) != 0:
            flags.append("reverse")
        if (val & 4) != 0:
            flags.append("on_parole")
        if (val & 8) != 0:
            flags.append("prioritize_partials")
        if (val & 16) != 0:
            flags.append("sequential")
        if (val & 64) != 0:
            flags.append("align_expanded_pieces")
        if (val & 128) != 0:
            flags.append("piece_extent_affinity")
        data = "|".join(flags)
    else:
        name = ""

    return "({}) {}".format(name, data)
