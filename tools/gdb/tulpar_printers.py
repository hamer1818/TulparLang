# Tulpar gdb pretty-printer'ı — `VMValue` taşıyıcısını okunur değere çevirir.
#
# NEDEN: `tulpar --debug build` her Tulpar yerelini DWARF'ta 128 bitlik opak
# bir `VMValue` temel tipi olarak bildiriyor (llvm_backend.cpp, PR 3f). gdb ve
# `tulpar debug` (DAP) bu yüzden `str ad = "Hamza"` için
# `130514698818214998946349060` basıyordu — etiket + yük, tek bir sayı olarak.
# Ölçüldü 2026-09-27. Bu betik etiketi ve yükü çözer:
#     ad = "Hamza"   f = 2.5   a = [1, 2, 3]   j = {"k": 7, "s": "x"}
#
# KULLANIM
#   gdb:  (gdb) source tools/gdb/tulpar_printers.py
#         ya da kurulu bir tulpar ile:  tulpar debug --gdb-script > t.py
#                                       (gdb) source t.py
#   DAP:  `tulpar debug` bu betiği gömülü taşıyor ve gdb'yi başlatınca yüklüyor.
#
# DÜZEN VARSAYIMI (src/vm/vm.hpp, LP64, küçük-sonlu — x86_64 ve AArch64):
#   VMValue   { uint32 type @0; <pad>; union as @8 (8 bayt) }        16 bayt
#   Obj       { uint32 type @0; next @8; arena @16; ref @20; moved @24 }  32 bayt
#   ObjString { Obj; int length @32; int capacity @36; char *chars @40 }
#   ObjArray  { Obj; int count @32; int cap @36; VMValue *items_ @40;
#               int64 *idata @48; int elem_bits @56 }
#   ObjObject { Obj; int count @32; int cap @36; ObjString **keys @40;
#               VMValue *values @48 }
# Kullanıcı ikilisi `libtulpar_runtime.a`'yı hata ayıklama bilgisiz linkliyor;
# yani bu düzen DWARF'tan okunamıyor, burada yazılı. Değişirse
# tests/dap_audit.py'nin "okunur değerler" senaryosu kızarır (kapı bu).
import struct

import gdb

VM_VAL_INT, VM_VAL_FLOAT, VM_VAL_BOOL, VM_VAL_VOID, VM_VAL_OBJ = range(5)
(OBJ_STRING, OBJ_ARRAY, OBJ_OBJECT, OBJ_FUNCTION, OBJ_STRUCT, OBJ_CLOSURE,
 OBJ_PROMISE, OBJ_STRUCT_ARRAY) = range(8)
OBJ_NAMES = ["string", "array", "object", "function", "struct", "closure",
             "promise", "struct_array"]

MAX_ITEMS = 16     # dizi/nesne başına gösterilen eleman
MAX_STR = 200      # gösterilen dizgi baytı
MAX_DEPTH = 3      # iç içe dizi/nesne derinliği


def _mem(addr, n):
    return bytes(gdb.selected_inferior().read_memory(addr, n))


def _u32(addr):
    return struct.unpack("<I", _mem(addr, 4))[0]


def _i32(addr):
    return struct.unpack("<i", _mem(addr, 4))[0]


def _u64(addr):
    return struct.unpack("<Q", _mem(addr, 8))[0]


def _quote(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def _string(obj):
    n = _i32(obj + 32)
    chars = _u64(obj + 40)
    if n < 0 or not chars:
        return '""'
    raw = _mem(chars, min(n, MAX_STR))
    txt = raw.decode("utf-8", "replace")
    return _quote(txt) + ("..." if n > MAX_STR else "")


def decode_raw(tag, payload, depth=0):
    """(etiket, 64-bit yük) -> okunur metin."""
    if tag == VM_VAL_INT:
        return str(struct.unpack("<q", struct.pack("<Q", payload))[0])
    if tag == VM_VAL_FLOAT:
        return repr(struct.unpack("<d", struct.pack("<Q", payload))[0])
    if tag == VM_VAL_BOOL:
        return "true" if payload & 0xFFFFFFFF else "false"
    if tag == VM_VAL_VOID:
        return "void"
    if tag != VM_VAL_OBJ:
        return "<VMValue etiket=%d yuk=0x%x>" % (tag, payload)
    obj = payload
    if not obj:
        return "null"
    try:
        otype = _u32(obj)
        if otype == OBJ_STRING:
            return _string(obj)
        if depth >= MAX_DEPTH:
            return "<%s>" % (OBJ_NAMES[otype] if otype < len(OBJ_NAMES) else otype)
        if otype == OBJ_ARRAY:
            count = _i32(obj + 32)
            items = _u64(obj + 40)
            idata = _u64(obj + 48)
            bits = _i32(obj + 56)
            shown = []
            for i in range(min(count, MAX_ITEMS)):
                if items:
                    t = _u32(items + 16 * i)
                    p = _u64(items + 16 * i + 8)
                    shown.append(decode_raw(t, p, depth + 1))
                elif idata:
                    if bits == 32:
                        shown.append(str(struct.unpack("<i", _mem(idata + 4 * i, 4))[0]))
                    else:
                        shown.append(str(struct.unpack("<q", _mem(idata + 8 * i, 8))[0]))
            more = ", ... (%d)" % count if count > MAX_ITEMS else ""
            return "[" + ", ".join(shown) + more + "]"
        if otype == OBJ_OBJECT:
            count = _i32(obj + 32)
            keys = _u64(obj + 40)
            vals = _u64(obj + 48)
            shown = []
            for i in range(min(count, MAX_ITEMS)):
                k = _u64(keys + 8 * i)
                ks = _string(k) if k else "?"
                t = _u32(vals + 16 * i)
                p = _u64(vals + 16 * i + 8)
                shown.append("%s: %s" % (ks, decode_raw(t, p, depth + 1)))
            more = ", ... (%d)" % count if count > MAX_ITEMS else ""
            return "{" + ", ".join(shown) + more + "}"
        name = OBJ_NAMES[otype] if otype < len(OBJ_NAMES) else str(otype)
        return "<%s @0x%x>" % (name, obj)
    except gdb.MemoryError:
        return "<okunamayan nesne @0x%x>" % obj


def _raw16(val):
    """VMValue'nun 16 baytı. `int(val)` 128 bitlik tamsayıda gdb 15'te
    düşüyor ("more than 8 bytes") — CI (Ubuntu 24.04, gdb 15.1) printer'ı
    bu yüzden boş değerle bıraktı, gdb 17'de çalıştı (ölçüldü 2026-09-28).
    Sıra: bellekteki adres (yereller -O0'da yığında) -> Value.bytes (gdb 14+)
    -> int()."""
    try:
        addr = int(val.address)
        if addr:
            return _mem(addr, 16)
    except (gdb.error, TypeError, ValueError):
        pass
    try:
        b = bytes(val.bytes)
        if len(b) >= 16:
            return b[:16]
    except (AttributeError, gdb.error, TypeError):
        pass
    n = int(val) & ((1 << 128) - 1)
    return n.to_bytes(16, "little")


class VMValuePrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        raw = _raw16(self.val)
        tag = struct.unpack("<I", raw[0:4])[0]
        payload = struct.unpack("<Q", raw[8:16])[0]
        return decode_raw(tag, payload)


def lookup(val):
    t = val.type.strip_typedefs()
    if t.name == "VMValue" and t.sizeof == 16 and t.code == gdb.TYPE_CODE_INT:
        return VMValuePrinter(val)
    return None


def register(objfile=None):
    target = objfile.pretty_printers if objfile is not None else gdb.pretty_printers
    if lookup not in target:
        target.append(lookup)


register()
