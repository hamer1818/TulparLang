#!/usr/bin/env python3
"""SPIR-V arayuz yansitmasi (reflection) — saf Python, dis arac gerektirmez.

Neden GLSL metnini degil SPIR-V'yi okuyoruz: std140/std430 ofsetlerini
DERLEYICI hesapliyor. GLSL kaynagini ayristirmak "kurali biz de uygulariz"
demektir; o zaman denetim, denetledigi seyin ayni varsayimini tekrarlar
(Tuzaklar: "tekrarlanan varsayim kendini gizler"). `OpMemberDecorate Offset`
ise glslc'nin GERCEKTE urettigi sayidir.

Girdi: engine/rhi/shaders/*_spv.h (depoya giren uretilmis uint32 dizileri) ya
da ham .spv dosyasi. Cikti: blok/alan agaci + vertex girdi konumlari.

Tek basina kosulursa envanteri basar:
    python3 engine/tools/spirv_reflect.py [engine/rhi/shaders]
"""
import os
import re
import sys

MAGIC = 0x07230203

# --- Opcode'lar (yalnizca gerekenler; SPIR-V 1.x cekirdegi) ---------------
OP_NAME = 5
OP_MEMBER_NAME = 6
OP_ENTRY_POINT = 15
OP_EXECUTION_MODE = 16
OP_TYPE_VOID = 19
OP_TYPE_BOOL = 20
OP_TYPE_INT = 21
OP_TYPE_FLOAT = 22
OP_TYPE_VECTOR = 23
OP_TYPE_MATRIX = 24
OP_TYPE_IMAGE = 25
OP_TYPE_SAMPLER = 26
OP_TYPE_SAMPLED_IMAGE = 27
OP_TYPE_ARRAY = 28
OP_TYPE_RUNTIME_ARRAY = 29
OP_TYPE_STRUCT = 30
OP_TYPE_POINTER = 32
OP_CONSTANT = 43
OP_SPEC_CONSTANT = 50
OP_VARIABLE = 59
OP_DECORATE = 71
OP_MEMBER_DECORATE = 72

# --- Dekorasyonlar --------------------------------------------------------
DEC_SPEC_ID = 1
DEC_BLOCK = 2
DEC_BUFFER_BLOCK = 3
DEC_ROW_MAJOR = 4
DEC_COL_MAJOR = 5
DEC_ARRAY_STRIDE = 6
DEC_MATRIX_STRIDE = 7
DEC_BUILTIN = 11
DEC_LOCATION = 30
DEC_BINDING = 33
DEC_DESCRIPTOR_SET = 34
DEC_OFFSET = 35

# --- Depolama siniflari ---------------------------------------------------
SC_UNIFORM_CONSTANT = 0
SC_INPUT = 1
SC_UNIFORM = 2
SC_OUTPUT = 3
SC_PUSH_CONSTANT = 9
SC_STORAGE_BUFFER = 12

_ARRAY_RE = re.compile(r"0x[0-9a-fA-F]{8}")


def words_from_header(path):
    """*_spv.h icindeki uint32 dizisini okur (compile_shaders.py'nin yazdigi bicim)."""
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    start = text.find("{")
    end = text.rfind("}")
    if start < 0 or end < 0:
        raise ValueError("%s: uint32 dizisi bulunamadi" % path)
    return [int(w, 16) for w in _ARRAY_RE.findall(text[start:end])]


def words_from_spv(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) % 4 or len(data) < 20:
        raise ValueError("%s: SPIR-V boyu 4'un kati degil" % path)
    return [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]


class Field(object):
    """Blok icinde MUTLAK ofsetli bir yaprak (skaler / vektor / matris)."""

    __slots__ = ("path", "kind", "offset", "size", "align", "array_stride", "matrix_stride",
                 "rows", "cols", "scalar", "row_major")

    def __init__(self, path, kind, offset, size, align, scalar, rows=1, cols=1,
                 array_stride=None, matrix_stride=None, row_major=False):
        self.path = path
        self.kind = kind            # "scalar" | "vector" | "matrix"
        self.offset = offset
        self.size = size
        self.align = align
        self.scalar = scalar        # "f32" | "i32" | "u32" | "f16" | "i16" ...
        self.rows = rows
        self.cols = cols
        self.array_stride = array_stride
        self.matrix_stride = matrix_stride
        self.row_major = row_major

    def type_name(self):
        pre = {"f32": "", "i32": "i", "u32": "u", "f64": "d", "bool": "b"}.get(self.scalar, self.scalar + "_")
        if self.kind == "scalar":
            return {"f32": "float", "i32": "int", "u32": "uint"}.get(self.scalar, self.scalar)
        if self.kind == "vector":
            return "%svec%d" % (pre, self.rows)
        return "%smat%dx%d" % (pre, self.cols, self.rows)

    def __repr__(self):
        s = "%-34s %-9s ofs=%-4d boy=%-3d hiz=%-2d" % (self.path, self.type_name(), self.offset,
                                                       self.size, self.align)
        if self.matrix_stride is not None:
            s += " matadim=%d%s" % (self.matrix_stride, " (satir-oncelikli)" if self.row_major else "")
        if self.array_stride is not None:
            s += " dizadim=%d" % self.array_stride
        return s


class Block(object):
    __slots__ = ("shader", "name", "instance", "kind", "set", "binding", "fields", "size",
                 "members", "runtime_array_stride")

    def __init__(self, shader, name, instance, kind, dset, binding):
        self.shader = shader
        self.name = name
        self.instance = instance
        self.kind = kind            # "uniform" | "push_constant" | "ssbo"
        self.set = dset
        self.binding = binding
        self.fields = []            # duzlestirilmis yapraklar (Field)
        self.members = []           # (ad, ofset, boyut) — ust duzey uyeler
        self.size = 0
        self.runtime_array_stride = None

    def key(self):
        if self.kind == "push_constant":
            return "push_constant:%s" % self.name
        return "%s:set%d.binding%d:%s" % (self.kind, self.set, self.binding, self.name)


class StageIo(object):
    __slots__ = ("name", "location", "type_name", "scalar", "rows")

    def __init__(self, name, location, type_name, scalar, rows):
        self.name = name
        self.location = location
        self.type_name = type_name
        self.scalar = scalar
        self.rows = rows


class Module(object):
    def __init__(self, words, shader="?"):
        if not words or words[0] != MAGIC:
            raise ValueError("%s: SPIR-V sihirli sayisi yok" % shader)
        self.shader = shader
        self.version = words[1]
        self.types = {}        # id -> dict
        self.names = {}        # id -> str
        self.member_names = {} # id -> {index: str}
        self.dec = {}          # id -> {decoration: [operands]}
        self.mdec = {}         # id -> {index: {decoration: [operands]}}
        self.constants = {}    # id -> int
        self.variables = []    # (id, type_id, storage_class)
        self.stage = None
        self.spec_constants = []  # (spec_id, name, value)
        self._parse(words)

    # --- ham ayristirma ---------------------------------------------------
    def _parse(self, w):
        i = 5
        n = len(w)
        while i < n:
            wc = w[i] >> 16
            op = w[i] & 0xFFFF
            if wc == 0:
                raise ValueError("%s: sifir uzunluklu komut @%d" % (self.shader, i))
            a = w[i + 1:i + wc]
            self._inst(op, a)
            i += wc

    @staticmethod
    def _literal_string(ops, start):
        bs = bytearray()
        for k in range(start, len(ops)):
            word = ops[k]
            for shift in (0, 8, 16, 24):
                c = (word >> shift) & 0xFF
                if c == 0:
                    return bs.decode("utf-8", "replace"), k + 1
                bs.append(c)
        return bs.decode("utf-8", "replace"), len(ops)

    def _inst(self, op, a):
        if op == OP_ENTRY_POINT:
            self.stage = {0: "vert", 1: "tesc", 2: "tese", 3: "geom", 4: "frag", 5: "comp"}.get(a[0], "?")
        elif op == OP_NAME:
            s, _ = self._literal_string(a, 1)
            self.names[a[0]] = s
        elif op == OP_MEMBER_NAME:
            s, _ = self._literal_string(a, 2)
            self.member_names.setdefault(a[0], {})[a[1]] = s
        elif op == OP_DECORATE:
            self.dec.setdefault(a[0], {})[a[1]] = list(a[2:])
        elif op == OP_MEMBER_DECORATE:
            self.mdec.setdefault(a[0], {}).setdefault(a[1], {})[a[2]] = list(a[3:])
        elif op == OP_CONSTANT:
            self.constants[a[1]] = a[2] if len(a) > 2 else 0
        elif op == OP_SPEC_CONSTANT:
            self.constants[a[1]] = a[2] if len(a) > 2 else 0
        elif op == OP_VARIABLE:
            self.variables.append((a[1], a[0], a[2]))
        elif op == OP_TYPE_VOID:
            self.types[a[0]] = {"k": "void"}
        elif op == OP_TYPE_BOOL:
            self.types[a[0]] = {"k": "bool"}
        elif op == OP_TYPE_INT:
            self.types[a[0]] = {"k": "int", "w": a[1], "signed": a[2]}
        elif op == OP_TYPE_FLOAT:
            self.types[a[0]] = {"k": "float", "w": a[1]}
        elif op == OP_TYPE_VECTOR:
            self.types[a[0]] = {"k": "vec", "comp": a[1], "n": a[2]}
        elif op == OP_TYPE_MATRIX:
            self.types[a[0]] = {"k": "mat", "col": a[1], "n": a[2]}
        elif op == OP_TYPE_ARRAY:
            self.types[a[0]] = {"k": "arr", "elem": a[1], "len_id": a[2]}
        elif op == OP_TYPE_RUNTIME_ARRAY:
            self.types[a[0]] = {"k": "rtarr", "elem": a[1]}
        elif op == OP_TYPE_STRUCT:
            self.types[a[0]] = {"k": "struct", "members": list(a[1:])}
        elif op == OP_TYPE_POINTER:
            self.types[a[0]] = {"k": "ptr", "sc": a[1], "pointee": a[2]}
        elif op in (OP_TYPE_IMAGE, OP_TYPE_SAMPLER, OP_TYPE_SAMPLED_IMAGE):
            self.types[a[0]] = {"k": "opaque"}

    # --- tip yardimcilari -------------------------------------------------
    def _scalar_name(self, tid):
        t = self.types.get(tid, {})
        if t.get("k") == "float":
            return "f%d" % t["w"]
        if t.get("k") == "int":
            return ("i" if t["signed"] else "u") + str(t["w"])
        if t.get("k") == "bool":
            return "bool"
        return "?"

    def _scalar_size(self, tid):
        t = self.types.get(tid, {})
        if t.get("k") in ("float", "int"):
            return t["w"] // 8
        if t.get("k") == "bool":
            return 4
        return 0

    def _array_len(self, t):
        return self.constants.get(t["len_id"], 0)

    def _flatten(self, tid, path, base, out, mdec_ctx=None, in_struct=None, member_idx=None):
        """tid tipini `base` ofsetinden baslayarak yapraklara acar."""
        t = self.types.get(tid)
        if t is None:
            return
        k = t["k"]
        md = {}
        if in_struct is not None and member_idx is not None:
            md = self.mdec.get(in_struct, {}).get(member_idx, {})
        if k in ("float", "int", "bool"):
            sz = self._scalar_size(tid)
            out.append(Field(path, "scalar", base, sz, sz, self._scalar_name(tid)))
        elif k == "vec":
            sz = self._scalar_size(t["comp"]) * t["n"]
            align = self._scalar_size(t["comp"]) * (4 if t["n"] == 3 else t["n"])
            out.append(Field(path, "vector", base, sz, align, self._scalar_name(t["comp"]), rows=t["n"]))
        elif k == "mat":
            col_t = self.types[t["col"]]
            rows = col_t["n"]
            scalar = self._scalar_name(col_t["comp"])
            mstride = md.get(DEC_MATRIX_STRIDE, [None])[0]
            row_major = DEC_ROW_MAJOR in md
            if mstride is None:
                mstride = self._scalar_size(col_t["comp"]) * (4 if rows == 3 else rows)
            count = rows if row_major else t["n"]
            out.append(Field(path, "matrix", base, mstride * count, mstride, scalar,
                             rows=rows, cols=t["n"], matrix_stride=mstride, row_major=row_major))
        elif k in ("arr", "rtarr"):
            stride = self.dec.get(tid, {}).get(DEC_ARRAY_STRIDE, [None])[0]
            if stride is None:
                stride = 0
            count = self._array_len(t) if k == "arr" else 1
            for e in range(count):
                start = len(out)
                self._flatten(t["elem"], "%s[%d]" % (path, e), base + e * stride, out,
                              in_struct=in_struct, member_idx=member_idx)
                if e == 0 and start < len(out):
                    out[start].array_stride = stride
        elif k == "struct":
            for mi, mt in enumerate(t["members"]):
                moff = self.mdec.get(tid, {}).get(mi, {}).get(DEC_OFFSET, [0])[0]
                mname = self.member_names.get(tid, {}).get(mi, "m%d" % mi)
                sub = "%s.%s" % (path, mname) if path else mname
                self._flatten(mt, sub, base + moff, out, in_struct=tid, member_idx=mi)
        # opaque / ptr / void: yaprak yok

    def _extent(self, tid):
        """Tipin bayt uzantisi (son yapragin sonu). Kuyruk dolgusunu SAYMAZ."""
        tmp = []
        self._flatten(tid, "", 0, tmp)
        return max((f.offset + f.size for f in tmp), default=0)

    # --- arayuz -----------------------------------------------------------
    def blocks(self):
        out = []
        for vid, ptr_id, sc in self.variables:
            if sc not in (SC_UNIFORM, SC_PUSH_CONSTANT, SC_STORAGE_BUFFER):
                continue
            ptr = self.types.get(ptr_id)
            if not ptr or ptr["k"] != "ptr":
                continue
            st_id = ptr["pointee"]
            st = self.types.get(st_id)
            if not st or st["k"] != "struct":
                continue
            decs = self.dec.get(st_id, {})
            if DEC_BLOCK not in decs and DEC_BUFFER_BLOCK not in decs:
                continue  # opak (sampler) ya da blok olmayan uniform
            if sc == SC_PUSH_CONSTANT:
                kind = "push_constant"
            elif sc == SC_STORAGE_BUFFER or DEC_BUFFER_BLOCK in decs:
                kind = "ssbo"
            else:
                kind = "uniform"
            vdec = self.dec.get(vid, {})
            b = Block(self.shader, self.names.get(st_id, "?"), self.names.get(vid, ""), kind,
                      vdec.get(DEC_DESCRIPTOR_SET, [-1])[0], vdec.get(DEC_BINDING, [-1])[0])
            self._flatten(st_id, "", 0, b.fields)
            for mi, mt in enumerate(st["members"]):
                moff = self.mdec.get(st_id, {}).get(mi, {}).get(DEC_OFFSET, [0])[0]
                mname = self.member_names.get(st_id, {}).get(mi, "m%d" % mi)
                b.members.append((mname, moff, self._extent(mt)))
                mt_t = self.types.get(mt, {})
                if mt_t.get("k") == "rtarr":
                    b.runtime_array_stride = self.dec.get(mt, {}).get(DEC_ARRAY_STRIDE, [0])[0]
            b.size = max((f.offset + f.size for f in b.fields), default=0)
            out.append(b)
        out.sort(key=lambda x: (x.kind, x.set, x.binding, x.name))
        return out

    def stage_inputs(self):
        out = []
        for vid, ptr_id, sc in self.variables:
            if sc != SC_INPUT:
                continue
            vdec = self.dec.get(vid, {})
            if DEC_BUILTIN in vdec or DEC_LOCATION not in vdec:
                continue
            ptr = self.types.get(ptr_id)
            if not ptr or ptr["k"] != "ptr":
                continue
            tmp = []
            self._flatten(ptr["pointee"], "", 0, tmp)
            if not tmp:
                continue
            f = tmp[0]
            out.append(StageIo(self.names.get(vid, "?"), vdec[DEC_LOCATION][0], f.type_name(),
                               f.scalar, f.rows))
        out.sort(key=lambda x: x.location)
        return out

    def specs(self):
        out = []
        for oid, d in sorted(self.dec.items()):
            if DEC_SPEC_ID in d:
                out.append((d[DEC_SPEC_ID][0], self.names.get(oid, "?"), self.constants.get(oid)))
        return out


def load_shader_dir(shader_dir):
    """Dizindeki her *_spv.h icin Module dondurur: {shader_adi: Module}."""
    mods = {}
    for name in sorted(os.listdir(shader_dir)):
        if not name.endswith("_spv.h"):
            continue
        stem = name[:-len("_spv.h")]
        # mesh_frag -> mesh.frag
        for ext in ("vert", "frag", "comp"):
            if stem.endswith("_" + ext):
                stem = stem[:-(len(ext) + 1)] + "." + ext
                break
        mods[stem] = Module(words_from_header(os.path.join(shader_dir, name)), stem)
    return mods


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(here), "rhi", "shaders")
    mods = load_shader_dir(d)
    total = 0
    for name in sorted(mods):
        m = mods[name]
        bl = m.blocks()
        total += len(bl)
        print("=== %s (%s, %d blok) ===" % (name, m.stage, len(bl)))
        for b in bl:
            print("  [%s] %s %s  boy=%d alan=%d%s" % (
                b.kind, b.key(), b.instance, b.size, len(b.fields),
                "" if b.runtime_array_stride is None else "  kosum-adimi=%d" % b.runtime_array_stride))
            for f in b.fields[:200]:
                print("      " + repr(f))
        for io in m.stage_inputs():
            print("  [girdi] location=%d %s %s" % (io.location, io.type_name, io.name))
        for sid, sname, sval in m.specs():
            print("  [ozel sabit] id=%d %s = %s" % (sid, sname, sval))
    print("TOPLAM: %d shader, %d blok" % (len(mods), total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
