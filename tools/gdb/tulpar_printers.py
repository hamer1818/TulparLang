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
#             (struct_tag: kutulu struct adi 1..255 — runtime'in
#             `g_struct_tag_names[256]` tablosunda; bu betik tabloyu sembol
#             adresinden okur ve nesneyi print gibi `P { x: 1 }` yazar,
#             2026-10-06. Tablo bulunamazsa json bicimi `{"x": 1}`.)
#   ObjString { Obj; int length @8; uint32 hash @12; char chars[] @16 }
#             (karakterler nesnenin ICINDE, isaretci degil — 2026-10-02)
#   ObjArray  { Obj; int count @8; int cap @12; VMValue *items_ @16;
#               int64 *idata @24; int elem_bits @32 }
#   ObjObject { Obj; int count @8; int cap @12; ObjString **keys @16;
#               VMValue *values @24 }
#   ObjStructArray { Obj; char *type_name @8; char **field_names @16;
#               int *field_types @24; int field_count @32; int count @36;
#               int cap @40; int elem_size @44; char *data @48 }
#             (elem_size 0: alan basina 8 B yuva, kod 0 int/1 float/2 bool;
#             > 0: C yerlesimi, field_types[fc + f] alan ofseti, kod 3 f32,
#             4 i32, 5 C bool — runtime sarr_field_value'nin birebir kopyasi)
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
ARR_ELEM_STR = -8   # ObjArray::elem_bits, kutusuz dizgi deposu: ObjString* tablosu (2026-10-06)
OBJ_HDR = 8        # sizeof(Obj) — src/vm/obj_layout.h TULPAR_OBJ_HEADER_SIZE

MAX_ITEMS = 16     # dizi/nesne başına gösterilen eleman
_TAG_TABLE = []    # [adres] — g_struct_tag_names (bulunamazsa [0])
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


def _cstr(addr, n=MAX_STR):
    """NUL ile biten C dizgisi (struct tip/alan adlari)."""
    if not addr:
        return None
    raw = _mem(addr, n)
    k = raw.find(b"\0")
    return (raw if k < 0 else raw[:k]).decode("utf-8", "replace")


def _tag_table():
    """Runtime'in kutulu struct ad tablosu (`static const char
    *g_struct_tag_names[256]`, runtime_bindings.cpp). Kullanici ikilisi
    runtime'i hata ayiklama bilgisiz linkliyor; tablo yalniz SEMBOL olarak
    var (yerel, sembol tablosunda). Adres bir kez cozulur."""
    if not _TAG_TABLE:
        addr = 0
        try:
            addr = int(gdb.parse_and_eval("(long)&'g_struct_tag_names'"))
        except gdb.error:
            try:
                sym = gdb.lookup_static_symbol("g_struct_tag_names")
                if sym is not None:
                    addr = int(sym.value().address)
            except (gdb.error, AttributeError):
                addr = 0
        _TAG_TABLE.append(addr)
    return _TAG_TABLE[0]


def _struct_tag_name(tag):
    t = _tag_table()
    if not tag or not t:
        return None
    return _cstr(_u64(t + 8 * tag))


def _is_tuple(tn):
    # Coklu donusun sentezlenmis struct'i (`__tup_<tip>_<tip>`): `(a, b)`.
    return tn is not None and tn.startswith("__tup_")


def _struct_text(tn, fields):
    """print(<struct>) ile ayni: `Ad { a: 1, b: 2.5 }`, tuple `(1, 2.5)`."""
    if _is_tuple(tn):
        return "(" + ", ".join(v for _, v in fields) + ")"
    if not fields:
        return "%s {}" % tn
    return "%s { %s }" % (tn, ", ".join("%s: %s" % (k or "_", v) for k, v in fields))


def _sarr_field(ftypes, fc, esz, e, f):
    code = _i32(ftypes + 4 * f) if ftypes else 0
    off = _i32(ftypes + 4 * (fc + f)) if (esz > 0 and ftypes) else 8 * f
    p = e + off
    if code == 1:
        return fmt_float(struct.unpack("<d", _mem(p, 8))[0])
    if code == 2:
        return "true" if _u64(p) else "false"
    if code == 3:
        return fmt_float(struct.unpack("<f", _mem(p, 4))[0])
    if code == 4:
        return str(_i32(p))
    if code == 5:
        return "true" if _u8(p) else "false"
    return str(struct.unpack("<q", _mem(p, 8))[0])


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
                    elif bits == ARR_ELEM_STR:
                        # Kutusuz dizgi deposu (split): eleman ObjString*.
                        sp = _u64(idata + 8 * i)
                        shown.append(_string(sp) if sp else "null")
                    else:
                        shown.append(str(struct.unpack("<q", _mem(idata + 8 * i, 8))[0]))
            more = ", ... (%d)" % count if count > MAX_ITEMS else ""
            return "[" + ", ".join(shown) + more + "]"
        if otype == OBJ_OBJECT:
            count = _i32(obj + OBJ_HDR)
            keys = _u64(obj + OBJ_HDR + 8)
            vals = _u64(obj + OBJ_HDR + 16)
            # Kutulu struct (Obj::struct_tag): print gibi `P { x: 1, ad: "z" }`.
            tn = _struct_tag_name(_u8(obj + 3))
            shown = []
            fields = []
            for i in range(min(count, MAX_ITEMS)):
                k = _u64(keys + 8 * i)
                if not k:
                    continue
                t = _u32(vals + 16 * i)
                p = _u64(vals + 16 * i + 8)
                v = decode_raw(t, p, depth + 1)
                fields.append((_string(k)[1:-1], v))
                shown.append("%s: %s" % (_string(k), v))
            more = ", ... (%d)" % count if count > MAX_ITEMS else ""
            if tn:
                return _struct_text(tn, fields)
            return "{" + ", ".join(shown) + more + "}"
        if otype == OBJ_STRUCT_ARRAY:
            # Tipli struct dizisi (`P[] d`): print gibi `[P { x: 1 }, ...]`.
            # Eskiden `<struct_array @0x...>` (2026-10-06'ya kadar).
            tn = _cstr(_u64(obj + 8)) or "struct"
            fnames = _u64(obj + 16)
            ftypes = _u64(obj + 24)
            fc = _i32(obj + 32)
            count = _i32(obj + 36)
            esz = _i32(obj + 44)
            data = _u64(obj + 48)
            step = esz if esz > 0 else 8 * fc
            shown = []
            for i in range(min(count, MAX_ITEMS)):
                e = data + step * i
                fields = []
                for f in range(fc):
                    fnm = _cstr(_u64(fnames + 8 * f)) if fnames else None
                    fields.append((fnm, _sarr_field(ftypes, fc, esz, e, f)))
                shown.append(_struct_text(tn, fields))
            more = ", ... (%d)" % count if count > MAX_ITEMS else ""
            return "[" + ", ".join(shown) + more + "]"
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
