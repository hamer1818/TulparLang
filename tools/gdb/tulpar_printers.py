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
#   Obj       { uint8 type @0; arena @1; moved @2; struct_tag @3; int32 ref @4 }  8 bayt
#             (struct_tag: kutulu struct adi, runtime tablosunda — bu betik
#             okumuyor, nesneyi `{k: v}` gosterir; 2026-10-02)
#   ObjString { Obj; int length @8; uint32 hash @12; char chars[] @16 }
#             (karakterler nesnenin ICINDE, isaretci degil — 2026-10-02)
#   ObjArray  { Obj; int count @8; int cap @12; VMValue *items_ @16;
#               int64 *idata @24; int elem_bits @32 }
#   ObjObject { Obj; int count @8; int cap @12; ObjString **keys @16;
#               VMValue *values @24 }
# Obj başlığı 2026-10-02'de 32 -> 8 bayt (src/vm/obj_layout.h); tür TEK
# bayt — 4 bayt okumak komşu alanları (ve kullanılmayan dolgu baytını) da
# okur. Kullanıcı ikilisi `libtulpar_runtime.a`'yı hata ayıklama bilgisiz
# linkliyor; yani bu düzen DWARF'tan okunamıyor, burada yazılı. Değişirse
# tests/dap_audit.py'nin "okunur değerler" senaryosu kızarır (kapı bu —
# 2026-10-02'de başlık küçülünce gerçekten kızardı).
import struct

import gdb

VM_VAL_INT, VM_VAL_FLOAT, VM_VAL_BOOL, VM_VAL_VOID, VM_VAL_OBJ = range(5)
(OBJ_STRING, OBJ_ARRAY, OBJ_OBJECT, OBJ_FUNCTION, OBJ_STRUCT, OBJ_CLOSURE,
 OBJ_PROMISE, OBJ_STRUCT_ARRAY) = range(8)
OBJ_NAMES = ["string", "array", "object", "function", "struct", "closure",
             "promise", "struct_array"]

ARR_ELEM_F64 = -64  # ObjArray::elem_bits, kutusuz double depo (vm.hpp)
OBJ_HDR = 8        # sizeof(Obj) — src/vm/obj_layout.h TULPAR_OBJ_HEADER_SIZE

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


def _u8(addr):
    return _mem(addr, 1)[0]


def _quote(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def _string(obj):
    n = _i32(obj + OBJ_HDR)
    chars = obj + OBJ_HDR + 8  # satir ici dizi: adres, yuklenen isaretci degil
    if n <= 0:
        return '""'
    raw = _mem(chars, min(n, MAX_STR))
    txt = raw.decode("utf-8", "replace")
    return _quote(txt) + ("..." if n > MAX_STR else "")


def fmt_float(d):
    """print(<float>) ile AYNI metin — src/vm/runtime_bindings.cpp
    aot_format_float'in birebir kopyası (Python'un `%g`si C'ninkiyle aynı):
    en kısa geri dönen `%.<p>g`, bilimsel gösterim yalnız |x| < 1e-4 ya da
    >= 1e16'da, NaN işaretsiz "nan", sonsuz "inf"/"-inf". 2026-10-02'ye kadar
    burada düz repr vardı: hata ayıklayıcı `f = 1.0`, program `1` diyordu
    (Tuzaklar 7j). repr'e yaslanmak YETMEZ: 1e16 <= |x| < 1e17 aralığında
    17 haneli sayılarda repr bilimsel, C'nin %.17g'si sabit yazıyor (3000
    rastgele değerde 41 fark, ölçüldü 2026-10-02)."""
    if d != d:
        return "nan"
    if d in (float("inf"), float("-inf")):
        return "inf" if d > 0 else "-inf"
    for prec in range(1, 18):
        s = "%.*g" % (prec, d)
        if float(s) != d:
            continue
        if "e" not in s:
            return s
        a = abs(d)
        if a < 1e-4 or a >= 1e16:
            return s
        f = "%.0f" % d
        return f if float(f) == d else s
    return "%.17g" % d


def decode_raw(tag, payload, depth=0):
    """(etiket, 64-bit yük) -> okunur metin — print(x) ile aynı kural."""
    if tag == VM_VAL_INT:
        return str(struct.unpack("<q", struct.pack("<Q", payload))[0])
    if tag == VM_VAL_FLOAT:
        return fmt_float(struct.unpack("<d", struct.pack("<Q", payload))[0])
    if tag == VM_VAL_BOOL:
        return "true" if payload & 0xFFFFFFFF else "false"
    if tag == VM_VAL_VOID:
        # print(null) "null" basar; "void" Tulpar'da görünen bir değer değil.
        return "null"
    if tag != VM_VAL_OBJ:
        return "<VMValue etiket=%d yuk=0x%x>" % (tag, payload)
    obj = payload
    if not obj:
        return "null"
    try:
        otype = _u8(obj)
        if otype == OBJ_STRING:
            return _string(obj)
        if depth >= MAX_DEPTH:
            return "<%s>" % (OBJ_NAMES[otype] if otype < len(OBJ_NAMES) else otype)
        if otype == OBJ_ARRAY:
            count = _i32(obj + OBJ_HDR)
            items = _u64(obj + OBJ_HDR + 8)
            idata = _u64(obj + OBJ_HDR + 16)
            bits = _i32(obj + OBJ_HDR + 24)
            shown = []
            for i in range(min(count, MAX_ITEMS)):
                if items:
                    t = _u32(items + 16 * i)
                    p = _u64(items + 16 * i + 8)
                    shown.append(decode_raw(t, p, depth + 1))
                elif idata:
                    if bits == 32:
                        shown.append(str(struct.unpack("<i", _mem(idata + 4 * i, 4))[0]))
                    elif bits == ARR_ELEM_F64:
                        # Kutusuz float[] (double depo): eskiden bit deseni
                        # int diye gösteriliyordu.
                        shown.append(fmt_float(struct.unpack("<d", _mem(idata + 8 * i, 8))[0]))
                    else:
                        shown.append(str(struct.unpack("<q", _mem(idata + 8 * i, 8))[0]))
            more = ", ... (%d)" % count if count > MAX_ITEMS else ""
            return "[" + ", ".join(shown) + more + "]"
        if otype == OBJ_OBJECT:
            count = _i32(obj + OBJ_HDR)
            keys = _u64(obj + OBJ_HDR + 8)
            vals = _u64(obj + OBJ_HDR + 16)
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
