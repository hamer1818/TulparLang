#!/usr/bin/env python3
"""CPU-GPU yerlesim denetimi (Faz 8.4) — shader bloklari ile C++ struct'lari.

NEDEN: bir `uniform`/`push_constant`/SSBO blogunun std140/std430 yerlesimi ile
C++ tarafindaki struct'in bayt yerlesimi SESSIZCE ayrisabilir. Ne derleyici, ne
linker, ne Vulkan dogrulama katmani bunu soyler: GPU baska bir ofsetten okur ve
goruntu "biraz yanlis" olur. Depoda elle yazilmis birkac `static_assert` var ama
(a) yalniz birkac blok icin, (b) cogu yalniz `sizeof` bakiyor — ayni BOYUTTA
alan sirasi degismis bir struct o kaliptan KACAR (pozitif kontrol ii).

UC KAYNAK, UC ROL — hicbiri otekinin varsayimini tekrarlamaz:
  1. YERLESIM  <- SPIR-V (`engine/rhi/shaders/*_spv.h`). `OpMemberDecorate
     Offset` glslc'nin GERCEKTE urettigi sayidir. GLSL metninden std140
     kurallarini yeniden hesaplasaydik denetim, denetledigi seyin ayni
     varsayimini tekrarlardi.
  2. ADLAR     <- GLSL kaynagi (blok/struct uye adlari; `glslc -O` OpName'leri
     siler). Yalniz ad ve BILDIRIM SIRASI aliniyor; uye sayisi SPIR-V ile
     tutmazsa KIRMIZI, yani ad ayristirmasi sessizce kayamaz.
  3. C++ YERLESIMI <- DERLEYICI. Struct metni basliktan OLDUGU GIBI cikarilir,
     uretilen bir sonda TU'suna konur, `&uye - &nesne` ve `sizeof` ile
     olculur. Elle hesaplanan tek bir sayi yok.

Eslestirme ELLE YAZILMIS BIR TABLO DEGIL: her SPIR-V blogu, uye ADLARI tutan
C++ struct'i basliklarda arayarak bulunur. Eslesmeyen blok ya `KAPSAM_DISI`
sozlugunde gerekcesiyle kayitlidir ya da KIRMIZI olur — kapsam sessizce
daralamaz.

Kosum:
    python3 engine/tools/layout_check.py            # denetim (cikis 0/1)
    python3 engine/tools/layout_check.py --kontrol  # + iki pozitif kontrol
    python3 engine/tools/layout_check.py --ayrinti  # her alani bas
"""
import os
import re
import shutil
import tempfile
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(HERE)
ROOT = os.path.dirname(ENGINE)
SHADERS = os.path.join(ENGINE, "rhi", "shaders")
# C++ tarafinda GPU struct'larinin arandigi basliklar.
CPP_HEADERS = [os.path.join(ENGINE, "renderer", "renderer.hpp"),
               os.path.join(ENGINE, "renderer", "cull.hpp")]

sys.path.insert(0, HERE)
import spirv_reflect as spv  # noqa: E402

# --- Kapsam disi bloklar: GEREKCELI kayit ---------------------------------
# Buraya bir sey eklemek BILINCLI bir karardir. Listede olmayan eslesmeyen
# blok KIRMIZI olur; kapsam sessizce daralamaz.
KAPSAM_DISI = {
    "push_constant:Push(screen,rot,encode)":
        "ui.vert — C++ tarafi struct DEGIL: renderer.cpp'de anonim `const float push[5]`.",
    "push_constant:Push(idx)":
        "motion.vert — C++ tarafi struct DEGIL: renderer.cpp'de anonim `const uint32_t idx[4]`.",
}

SCALAR_KIND = {"f32": "float", "i32": "int32_t", "u32": "uint32_t"}


# =========================================================================
# 1) GLSL: blok/struct ADLARI (yerlesim DEGIL)
# =========================================================================
_COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
_BLOCK = re.compile(
    r"layout\s*\(([^)]*)\)\s*"
    r"((?:readonly\s+|writeonly\s+|coherent\s+|restrict\s+)*)"
    r"(uniform|buffer)\s+(\w+)\s*\{([^{}]*)\}\s*(\w*)\s*;", re.S)
_STRUCT = re.compile(r"\bstruct\s+(\w+)\s*\{([^{}]*)\}\s*;", re.S)
_MEMBER = re.compile(r"\b(\w+)\s+(\w+)\s*((?:\[[^\]]*\])*)\s*;")


def _members(body):
    out = []
    for m in _MEMBER.finditer(body):
        dims = re.findall(r"\[([^\]]*)\]", m.group(3) or "")
        out.append({"type": m.group(1), "name": m.group(2), "dims": dims})
    return out


def glsl_names(path):
    """Bir shader kaynagindan blok ve struct uye ADLARINI cikarir."""
    with open(path, encoding="utf-8", errors="replace") as f:
        text = _COMMENT.sub(" ", f.read())
    structs = {m.group(1): _members(m.group(2)) for m in _STRUCT.finditer(text)}
    blocks = []
    for m in _BLOCK.finditer(text):
        quals, _ro, kw, name, body, inst = m.groups()
        q = {k.strip(): True for k in quals.split(",")}
        push = any(k.startswith("push_constant") for k in q)
        dset = binding = -1
        for k in q:
            mm = re.match(r"set\s*=\s*(\d+)", k)
            if mm:
                dset = int(mm.group(1))
            mm = re.match(r"binding\s*=\s*(\d+)", k)
            if mm:
                binding = int(mm.group(1))
        blocks.append({"name": name, "instance": inst, "members": _members(body),
                       "kind": "push_constant" if push else ("ssbo" if kw == "buffer" else "uniform"),
                       "set": dset, "binding": binding})
    return blocks, structs


# =========================================================================
# 2) SPIR-V yapraklarina GLSL adlarini giydir
# =========================================================================
_PATHTOK = re.compile(r"\.?m(\d+)|\[(\d+)\]")


def rename_path(path, block_members, structs):
    """`m0[2].m1` -> `light_viewproj[2].foo` (adlar GLSL'den, sira SPIR-V'den)."""
    out = []
    cur = block_members
    pos = 0
    for tk in _PATHTOK.finditer(path):
        if tk.start() != pos:
            return None  # beklenmeyen bicim
        pos = tk.end()
        if tk.group(1) is not None:
            idx = int(tk.group(1))
            if cur is None or idx >= len(cur):
                return None
            mem = cur[idx]
            out.append(("." if out else "") + mem["name"])
            cur = structs.get(mem["type"])
        else:
            out.append("[%s]" % tk.group(2))
    if pos != len(path):
        return None
    return "".join(out)


def collect_gpu_blocks(verbose=False):
    """Her shader icin SPIR-V bloklarini GLSL adlariyla dondurur."""
    mods = spv.load_shader_dir(SHADERS)
    result = []
    errors = []
    for shader in sorted(mods):
        mod = mods[shader]
        src = os.path.join(SHADERS, shader)
        if not os.path.exists(src):
            errors.append("%s: GLSL kaynagi yok" % shader)
            continue
        gblocks, gstructs = glsl_names(src)
        sblocks = mod.blocks()
        # SPIR-V blogunu GLSL bildirimiyle esle: push tek, otekiler set/binding.
        for sb in sblocks:
            cand = [g for g in gblocks if g["kind"] == sb.kind and
                    (sb.kind == "push_constant" or (g["set"] == sb.set and g["binding"] == sb.binding))]
            if len(cand) != 1:
                errors.append("%s: SPIR-V blogu [%s set=%d binding=%d] icin GLSL bildirimi "
                              "%d aday (1 bekleniyordu)" % (shader, sb.kind, sb.set, sb.binding, len(cand)))
                continue
            g = cand[0]
            if len(g["members"]) != len(sb.members):
                errors.append("%s blok %s: GLSL %d uye, SPIR-V %d uye — ad ayristirmasi kaydi"
                              % (shader, g["name"], len(g["members"]), len(sb.members)))
                continue
            named = []
            ok = True
            for f in sb.fields:
                p = rename_path(f.path, g["members"], gstructs)
                if p is None:
                    errors.append("%s blok %s: '%s' yolu adlandirilamadi" % (shader, g["name"], f.path))
                    ok = False
                    break
                named.append((p, f))
            if not ok:
                continue
            sb.name = g["name"]
            sb.instance = g["instance"]
            result.append({"shader": shader, "spv": sb, "glsl": g, "structs": gstructs,
                           "leaves": named})
    return result, errors


# =========================================================================
# 3) C++ tarafi: basliktan OLDUGU GIBI cikar, DERLEYICIYE olctur
# =========================================================================
_CPP_STRUCT = re.compile(r"\bstruct\s+(\w+)\s*\{(.*?)\n(\s*)\};", re.S)
_CPP_MEMBER = re.compile(r"^\s*(?:mutable\s+)?([A-Za-z_][\w:]*)\s+([A-Za-z_]\w*)\s*((?:\[[^\]]*\])*)\s*(?:=[^;]*)?;\s*$")


def cpp_candidates():
    """Basliklardaki DUZ (POD) struct'lar: {ad: {"text": ..., "members": [...]}}"""
    out = {}
    for h in CPP_HEADERS:
        with open(h, encoding="utf-8", errors="replace") as f:
            raw = f.read()
        text = _COMMENT.sub("", raw)
        for m in _CPP_STRUCT.finditer(text):
            name, body = m.group(1), m.group(2)
            if "(" in body or "struct" in body or "union" in body:
                continue  # uye fonksiyon / ic ice tip: duz POD degil
            members = []
            bad = False
            for line in body.splitlines():
                if not line.strip():
                    continue
                mm = _CPP_MEMBER.match(line)
                if not mm:
                    bad = True
                    break
                members.append({"type": mm.group(1), "name": mm.group(2),
                                "dims": re.findall(r"\[([^\]]*)\]", mm.group(3) or "")})
            if bad or not members:
                continue
            decl = "struct %s {%s\n};" % (name, body.rstrip())
            out.setdefault(name, {"text": decl, "members": members, "header": h})
    return out


def alias(gpu_name, cpp_members):
    """`pad0` <-> `pad[0]`: GLSL'de ayri uyeler, C++'ta dizi. Yerlesim yine
    ofsetten olculuyor; bu yalnizca AD esleme kurali."""
    names = {m["name"]: m for m in cpp_members}
    if gpu_name in names:
        return gpu_name
    mm = re.match(r"^(\w+?)(\d+)$", gpu_name)
    if mm and mm.group(1) in names and names[mm.group(1)]["dims"]:
        return "%s[%s]" % (mm.group(1), mm.group(2))
    return None


def match_struct(block_members, cands, kind):
    """Uye ADLARINA gore C++ karsiligini bul. ELLE YAZILMIS TABLO YOK.

    Kural: C++ struct'inin ilk N uyesi, GPU blogunun N uyesiyle AYNI SIRADA
    ayni adi tasimali. uniform/ssbo icin N = C++ uye sayisi (tam ortusme);
    push sabitinde shader blogun yalniz ONUNU bildirebilir, orada onek serbest.
    """
    want = [m["name"] for m in block_members]
    hits = []
    for cname, c in cands.items():
        mapped = [alias(w, c["members"]) for w in want]
        if any(x is None for x in mapped):
            continue
        # `pad0..pad2` <-> `pad[3]`: yalniz alias GERCEKTEN dizi indeksine
        # cevirdiginde o uyeyi acalim (`tex` / `tex2` gibi adlari BOZMASIN).
        expand = set()
        for v in mapped:
            mm = re.match(r"^(\w+)\[(\d+)\]$", v)
            if mm:
                expand.add(mm.group(1))
        flat = []
        for m in c["members"]:
            if m["name"] in expand and m["dims"] and m["dims"][0].isdigit():
                flat.extend("%s[%d]" % (m["name"], i) for i in range(int(m["dims"][0])))
            else:
                flat.append(m["name"])
        if flat[:len(mapped)] != mapped:
            continue
        extra = len(flat) - len(mapped)
        if kind != "push_constant" and extra:
            continue  # uniform/ssbo: TAM ortusme sart
        hits.append((cname, mapped, extra))
    if not hits:
        return []
    # En az "fazlalik" olan aday kazanir: `Push{model,color}` hem Push'a (1
    # fazla) hem GpuDrawItem'a (2 fazla) onek olur; dogrusu Push'tur.
    best = min(h[2] for h in hits)
    hits = [h for h in hits if h[2] == best]
    return [(c, m, e != 0) for c, m, e in hits]


PROBE_HEAD = """// URETILMIS SONDA — layout_check.py. Struct metinleri basliktan OLDUGU GIBI.
#include "renderer/renderer.hpp"
#include "renderer/cull.hpp"
#include <cstdio>
#include <cstddef>
#include <cstdint>
using namespace tulpar::engine;
using namespace tulpar::engine::renderer;
"""
# Struct metinlerinde gecen kXxx sabitleri Renderer'in ICINDE tanimli; sondada
# namespace kapsamina tasinmalari gerekiyor. Hangilerinin gerektigini METINDEN
# buluyoruz (elle liste yok) — biri public degilse derleme hatasi KIRMIZI olur.
_KCONST = re.compile(r"\bk[A-Z]\w*")


def build_probe(needed, cands, overrides, out_cpp):
    """needed: {struct_adi: [c++ ifade yolu, ...]} -> sonda TU'su uret."""
    texts = [overrides.get(n) or cands[n]["text"] for n in sorted(needed)]
    consts = sorted({c for t in texts for c in _KCONST.findall(t)})
    body = [PROBE_HEAD]
    for c in consts:
        body.append("static constexpr auto %s = Renderer::%s;" % (c, c))
    body.append("namespace sonda {")
    for t in texts:
        body.append(t)
        body.append("")
    body.append("} // namespace sonda\n")
    body.append("int main() {\n")
    for name in sorted(needed):
        body.append("  {\n    sonda::%s o{};\n    const char *b = reinterpret_cast<const char *>(&o);\n"
                    "    std::printf(\"S|%s|%%zu|%%zu\\n\", sizeof(o), alignof(sonda::%s));\n"
                    % (name, name, name))
        for path in needed[name]:
            body.append("    std::printf(\"F|%s|%s|%%zu|%%zu|%%zu\\n\", "
                        "(size_t)(reinterpret_cast<const char *>(&o.%s) - b), "
                        "sizeof(o.%s), alignof(decltype(o.%s)));\n"
                        % (name, path, path, path, path))
        body.append("    (void)b;\n  }\n")
    body.append("  return 0;\n}\n")
    with open(out_cpp, "w") as f:
        f.write("\n".join(body))


_TMP = None


def _tmp_dir():
    """Sonda TU'sunun yazilacagi dizin. Depoya HICBIR SEY yazilmaz."""
    global _TMP
    if os.environ.get("TULPAR_LAYOUT_TMP"):
        return os.environ["TULPAR_LAYOUT_TMP"]
    if _TMP is None:
        _TMP = tempfile.mkdtemp(prefix="tulpar_yerlesim_")
    return _TMP


def run_probe(out_dir, needed, cands, overrides):
    cpp = os.path.join(out_dir, "layout_probe.cpp")
    exe = os.path.join(out_dir, "layout_probe")
    build_probe(needed, cands, overrides, cpp)
    cxx = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        return None, "C++ derleyici yok (g++/clang++)"
    cmd = [cxx, "-std=c++17", "-fno-exceptions", "-fno-rtti", "-w",
           "-I", ENGINE, "-I", os.path.join(ENGINE, "third_party", "vulkan"), "-o", exe, cpp]
    r = subprocess.run(cmd, capture_output=True)
    if r.returncode != 0:
        return None, "sonda derlenmedi:\n" + r.stderr.decode("utf-8", "replace")[:4000]
    r = subprocess.run([exe], capture_output=True)
    if r.returncode != 0:
        return None, "sonda kosmadi (%d)" % r.returncode
    sizes, fields = {}, {}
    for line in r.stdout.decode().splitlines():
        p = line.split("|")
        if p[0] == "S":
            sizes[p[1]] = (int(p[2]), int(p[3]))
        elif p[0] == "F":
            fields[(p[1], p[2])] = (int(p[3]), int(p[4]), int(p[5]))
    return (sizes, fields), None


# =========================================================================
# 4) Karsilastirma
# =========================================================================
def _classify(b, cands):
    """Bir blok ornegini karsilastirma hedefine indirger.

    SSBO/UBO kalibi `Blok { Eleman e[]; }` ise ELEMAN struct'i karsilastirilir
    (dizi adimi ayrica sizeof(eleman) ile olculur). Eleman adlandirilmis bir
    struct degilse (skaler/vektor kosum dizisi) C++ tarafinda struct yoktur;
    o blok ayri kovaya dusar ve yalniz adim tutarliligi olculur.
    """
    sb, g = b["spv"], b["glsl"]
    gm = g["members"]
    if len(gm) == 1 and gm[0]["dims"]:
        if gm[0]["type"] in b["structs"]:
            return {"members": b["structs"][gm[0]["type"]], "name": gm[0]["type"],
                    "prefix": "%s[0]." % gm[0]["name"],
                    "stride": sb.fields[0].array_stride if sb.fields else None,
                    "scalar_array": None}
        return {"members": None, "name": gm[0]["type"], "prefix": "",
                "stride": sb.fields[0].array_stride if sb.fields else None,
                "scalar_array": (gm[0]["type"], gm[0]["name"],
                                 sb.fields[0].size if sb.fields else 0)}
    return {"members": gm, "name": g["name"], "prefix": "", "stride": None, "scalar_array": None}


def _cpp_path(p, prefix, cname, cands):
    """GPU yaprak yolunu C++ ifadesine cevirir (ad takmasi dahil)."""
    if prefix:
        if not p.startswith(prefix):
            return False  # bu yaprak baska bir dizi ELEMANINDA: karsilastirilmaz
        p = p[len(prefix):]
    head = re.match(r"^(\w+)", p)
    if head:
        a = alias(head.group(1), cands[cname]["members"])
        if a is None:
            return None
        if a != head.group(1):
            p = a + p[head.end():]
    return p


def audit(verbose=False, overrides=None):
    overrides = overrides or {}
    out = []
    def say(line=""):
        out.append(line)

    blocks, errors = collect_gpu_blocks()
    cands = cpp_candidates()
    say("SPIR-V: %d blok ornegi, %d shader" % (len(blocks), len(set(b["shader"] for b in blocks))))
    say("C++ aday struct (renderer.hpp + cull.hpp): %d" % len(cands))

    # --- B: her ornek icin hedef + C++ eslesmesi --------------------------
    ambiguous = []
    for b in blocks:
        t = _classify(b, cands)
        b["t"] = t
        b["cname"] = None
        b["is_prefix"] = False
        if t["members"] is None:
            continue
        hits = match_struct(t["members"], cands, b["spv"].kind)
        if len(hits) == 1:
            b["cname"], _, b["is_prefix"] = hits[0]
        elif len(hits) > 1:
            ambiguous.append((b["shader"], t["name"], [h[0] for h in hits]))

    # --- C: gruplama -------------------------------------------------------
    # push sabitleri set/binding TASIMAZ: ayni adli iki "Push" farkli boru
    # hatlarina ait olabilir (mesh'in Push'u ile ui.vert'in Push'u gibi).
    # Bu yuzden push gruplari C++ karsiligina (ya da uye imzasina) gore
    # kurulur; uniform/ssbo icin set/binding zaten tekil anahtardir.
    groups = {}
    for b in blocks:
        sb = b["spv"]
        if sb.kind == "push_constant":
            key = "push_constant:%s" % (b["cname"] or
                                        "%s(%s)" % (sb.name, ",".join(m["name"] for m in b["glsl"]["members"])))
        else:
            key = "%s:set%d.binding%d:%s" % (sb.kind, sb.set, sb.binding, sb.name)
        groups.setdefault(key, []).append(b)

    cross = 0
    say()
    say("--- Ayni blok birden cok shader'da: TEK yerlesim ---")
    multi = 0
    for key, grp in sorted(groups.items()):
        if len(grp) < 2:
            continue
        multi += 1
        ref = max(grp, key=lambda g: len(g["leaves"]))
        rmap = {p: f for p, f in ref["leaves"]}
        bad = 0
        for g in grp:
            if g is ref:
                continue
            for p, f in g["leaves"]:
                rf = rmap.get(p)
                if rf is None:
                    say("  KIRMIZI %s: %s alani %s'de var, %s'de yok" % (key, p, g["shader"], ref["shader"]))
                    bad += 1
                elif (rf.offset, rf.size) != (f.offset, f.size):
                    say("  KIRMIZI %s alan %s: %s ofs=%d boy=%d != %s ofs=%d boy=%d"
                        % (key, p, g["shader"], f.offset, f.size, ref["shader"], rf.offset, rf.size))
                    bad += 1
        cross += bad
        if verbose or bad:
            say("  %-40s %d shader (%s) %s" % (key, len(grp), ", ".join(sorted(g["shader"] for g in grp)),
                                               "KIRMIZI" if bad else "tamam"))
    say("  %d blok birden cok shader'da, %d uyusmazlik" % (multi, cross))

    # --- D: sonda plani ----------------------------------------------------
    reps = {k: max(v, key=lambda g: len(g["leaves"])) for k, v in groups.items()}
    plan = {}
    bad_path = []
    for key, b in sorted(reps.items()):
        if not b["cname"]:
            continue
        cname = b["cname"]
        plan.setdefault(cname, [])
        for p, f in b["leaves"]:
            q = _cpp_path(p, b["t"]["prefix"], cname, cands)
            if q is False:
                continue
            if q is None:
                bad_path.append("%s: '%s' C++ ifadesine cevrilemedi" % (key, p))
            elif q not in plan[cname]:
                plan[cname].append(q)

    if not plan:
        say("KIRMIZI: hicbir blok C++ struct'ina eslesmedi — denetim OLCMUYOR")
        return 1, out

    probe, perr = run_probe(_tmp_dir(), plan, cands, overrides)
    if probe is None:
        say("KIRMIZI: " + perr)
        return 1, out
    sizes, fields = probe

    mismatches = 0
    say()
    say("--- SPIR-V blogu <-> C++ struct (alan alan) ---")
    uncovered, scalar_arrays, pairs = [], [], 0
    for key, b in sorted(reps.items()):
        sb, t = b["spv"], b["t"]
        if t["scalar_array"] is not None:
            ty, nm, nat = t["scalar_array"]
            ok = t["stride"] == nat
            scalar_arrays.append((key, ty, nm, t["stride"], nat, ok))
            if not ok:
                mismatches += 1
            continue
        if not b["cname"]:
            uncovered.append((key, t["name"], [m["name"] for m in t["members"]]))
            continue
        pairs += 1
        cname = b["cname"]
        lines = []
        leaves = [(p, f) for p, f in b["leaves"] if not t["prefix"] or p.startswith(t["prefix"])]
        for p, f in leaves:
            q = _cpp_path(p, t["prefix"], cname, cands)
            cf = fields.get((cname, q)) if isinstance(q, str) else None
            if cf is None:
                lines.append("    KIRMIZI %-28s C++ tarafinda alan YOK" % (q or p))
                continue
            if cf[0] != f.offset or cf[1] != f.size:
                lines.append("    KIRMIZI %-28s GPU ofs=%-4d boy=%-3d (%s) | C++ ofs=%-4d boy=%-3d | "
                             "ofset farki %+d, boyut farki %+d"
                             % (q, f.offset, f.size, f.type_name(), cf[0], cf[1],
                                cf[0] - f.offset, cf[1] - f.size))
            elif verbose:
                extra = "" if f.matrix_stride is None else " matadim=%d" % f.matrix_stride
                extra += "" if f.array_stride is None else " dizadim=%d" % f.array_stride
                lines.append("    tamam   %-28s ofs=%-4d boy=%-3d hiz=%-2d %s%s"
                             % (q, f.offset, f.size, f.align, f.type_name(), extra))
        offs = [f.offset for _, f in leaves]
        extent = max((f.offset + f.size for _, f in leaves), default=0) - (min(offs) if t["prefix"] else 0)
        csize = sizes[cname][0]
        if t["stride"] is not None and t["stride"] != csize:
            lines.append("    KIRMIZI dizi adimi: GPU %d | sizeof(%s) = %d" % (t["stride"], cname, csize))
        note = ""
        if csize < extent:
            lines.append("    KIRMIZI boyut: GPU %d bayt okuyor, sizeof(%s) = %d — GPU STRUCT'I TASAR"
                         % (extent, cname, csize))
        elif csize > extent:
            if sb.kind == "push_constant":
                note = "  (ONEK: shader blogun yalniz ilk %d baytini bildiriyor)" % extent
            else:
                lines.append("    KIRMIZI boyut: GPU blogu %d bayt, sizeof(%s) = %d" % (extent, cname, csize))
        red = [x for x in lines if "KIRMIZI" in x]
        mismatches += len(red)
        say("  %-46s -> %-16s %2d alan, %3d bayt%s  %s"
            % (key, cname, len(leaves), extent, note, "KIRMIZI" if red else "tamam"))
        for ln in (lines if verbose else red):
            say(ln)

    if scalar_arrays:
        say()
        say("--- Skaler/vektor kosum dizileri (C++ tarafi duz dizi; struct yok) ---")
        for key, ty, nm, stride, nat, ok in scalar_arrays:
            say("  %-46s %s %s[]  adim=%s dogal=%s  %s" % (key, ty, nm, stride, nat,
                                                           "tamam" if ok else "KIRMIZI"))

    if ambiguous:
        say()
        say("--- COK ANLAMLI eslesme (KIRMIZI) ---")
        for sh, nm, names in ambiguous:
            say("  %s / %s -> %s" % (sh, nm, ", ".join(names)))
            mismatches += 1
    if uncovered:
        say()
        say("--- Eslesmeyen bloklar ---")
        for key, nm, names in uncovered:
            reason = KAPSAM_DISI.get(key)
            if reason:
                say("  KAPSAM DISI %-42s %s" % (key, reason))
            else:
                say("  KIRMIZI %-42s (%s: %s) — C++ karsiligi yok ve KAPSAM_DISI'nda kayitli degil"
                    % (key, nm, ", ".join(names)))
                mismatches += 1
    for e in errors + bad_path:
        say("KIRMIZI ayristirma: " + e)
    mismatches += len(errors) + len(bad_path) + cross

    say()
    say("SONUC: %d blok ornegi, %d grup, %d blok<->struct cifti, %d skaler dizi, "
        "%d kapsam disi, %d UYUSMAZLIK"
        % (len(blocks), len(groups), pairs, len(scalar_arrays), len(uncovered), mismatches))
    return (1 if mismatches else 0), out


# =========================================================================
# 5) Tazelik (istege bagli): depodaki *_spv.h gercekten bu GLSL'den mi?
# =========================================================================
def freshness():
    glslc = shutil.which("glslc")
    if not glslc:
        print("  ATLANDI: glslc yok — depodaki *_spv.h'nin GLSL ile tazeligi OLCULMEDI")
        print("  UYARI: bayat bir *_spv.h bu denetimi de yaniltir (yerlesim eski shader'dan okunur)")
        return 0
    bad = 0
    for name in sorted(os.listdir(SHADERS)):
        if not name.endswith((".vert", ".frag", ".comp")):
            continue
        r = subprocess.run([glslc, "-O", "--target-env=vulkan1.1", "-o", "-",
                            os.path.join(SHADERS, name)], capture_output=True)
        if r.returncode != 0:
            print("  KIRMIZI %s derlenmedi" % name)
            bad += 1
            continue
        fresh = [int.from_bytes(r.stdout[i:i + 4], "little") for i in range(0, len(r.stdout), 4)]
        hdr = os.path.join(SHADERS, name.replace(".", "_") + "_spv.h")
        if not os.path.exists(hdr):
            print("  KIRMIZI %s: uretilmis baslik yok" % name)
            bad += 1
            continue
        if spv.words_from_header(hdr) != fresh:
            print("  KIRMIZI %s: depodaki *_spv.h GLSL kaynagiyla ayni degil (BAYAT)" % name)
            bad += 1
    print("  glslc tazelik: %d shader, %d bayat" % (
        len([n for n in os.listdir(SHADERS) if n.endswith((".vert", ".frag", ".comp"))]), bad))
    return bad


# =========================================================================
# 6) Pozitif kontroller
# =========================================================================
def _swap_first_two_members(text):
    """Struct metninde ILK IKI uye bildirimini yer degistirir. Boyut DEGISMEZ."""
    head, body = text.split("{", 1)
    body, tail = body.rsplit("}", 1)
    lines = [l for l in body.splitlines() if l.strip()]
    if len(lines) < 2:
        raise RuntimeError("kontrol icin en az 2 uye gerekiyor")
    lines[0], lines[1] = lines[1], lines[0]
    return head + "{\n" + "\n".join(lines) + "\n}" + tail


def bozuk_kaydirma(cands, hedef="PostPush"):
    """(i) Bir alani 4 bayt KAYDIRAN sahte tanim: basa bir float sokulur."""
    t = cands[hedef]["text"]
    return {hedef: t.replace("struct %s {" % hedef,
                             "struct %s {\n  float _bozuk_kaydirma;" % hedef, 1)}


def bozuk_sira(cands, hedef="MaterialUbo"):
    """(ii) AYNI BOYUTTA, alan sirasi degismis tanim. `static_assert(sizeof)`
    kalibinin KOR NOKTASI: boyut degismedigi icin o assert yesil kalir."""
    return {hedef: _swap_first_two_members(cands[hedef]["text"])}


def assert_kor_noktasi(cands, hedef, bozuk):
    """Depodaki `static_assert(sizeof(X) == N)` bozuk tanimda da GECIYOR mu?

    Kontrol (ii)'nin degeri buna bagli: alan sirasi degisti ama boyut ayni
    kaldiysa, bugunku kapi (sizeof assert'i) YESIL kalir — yani o kapi bu hata
    sinifini HIC olcmuyor. Bunu iddia etmek yerine DERLEYICIYE sorduruyoruz.
    """
    src = open(cands[hedef]["header"], encoding="utf-8", errors="replace").read()
    m = re.search(r"static_assert\(sizeof\(%s\)\s*==\s*(\d+)" % re.escape(hedef), src)
    if not m:
        return "  (depoda sizeof(%s) icin static_assert yok)" % hedef
    n = int(m.group(1))
    tmp = _tmp_dir()
    f = os.path.join(tmp, "kor_nokta.cpp")
    consts = sorted(set(_KCONST.findall(bozuk[hedef])))
    with open(f, "w") as fh:
        fh.write(PROBE_HEAD)
        for c in consts:
            fh.write("static constexpr auto %s = Renderer::%s;\n" % (c, c))
        fh.write(bozuk[hedef] + "\n")
        fh.write("static_assert(sizeof(%s) == %d, \"depodaki mevcut kapi\");\n" % (hedef, n))
        fh.write("int main() { return 0; }\n")
    cxx = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    r = subprocess.run([cxx, "-std=c++17", "-fno-exceptions", "-fno-rtti", "-w", "-fsyntax-only",
                        "-I", ENGINE, "-I", os.path.join(ENGINE, "third_party", "vulkan"), f],
                       capture_output=True)
    if r.returncode == 0:
        return ("  OLCULDU: bozuk tanimda `static_assert(sizeof(%s) == %d)` HALA GECIYOR — "
                "bugunku kapi bu hatayi GORMUYOR." % (hedef, n))
    return "  NOT: bozuk tanim mevcut static_assert'i de dusuruyor (kor nokta degil)."


def main():
    verbose = "--ayrinti" in sys.argv
    print("=== CPU-GPU yerlesim denetimi (Faz 8.4) ===")
    rc, lines = audit(verbose=verbose)
    print("\n".join(lines))
    print()
    print("--- Uretilmis SPIR-V tazeligi ---")
    stale = freshness()
    if stale:
        rc = 1

    if "--kontrol" in sys.argv:
        cands = cpp_candidates()
        print()
        print("=== POZITIF KONTROL 1: alan 4 bayt kaydirildi (PostPush) ===")
        rc1, l1 = audit(overrides=bozuk_kaydirma(cands))
        print("\n".join([x for x in l1 if "KIRMIZI" in x or x.startswith("SONUC")]))
        print("  -> cikis %d (KIRMIZI bekleniyor)" % rc1)
        print()
        print("=== POZITIF KONTROL 2: ayni boyut, alan sirasi degisti (MaterialUbo) ===")
        bz = bozuk_sira(cands)
        print(assert_kor_noktasi(cands, "MaterialUbo", bz))
        rc2, l2 = audit(overrides=bz)
        print("\n".join([x for x in l2 if "KIRMIZI" in x or x.startswith("SONUC")]))
        print("  -> cikis %d (KIRMIZI bekleniyor)" % rc2)
        if rc1 == 0 or rc2 == 0:
            print("\nKAPI BOZUK: pozitif kontrol KIRMIZI olmadi — bu denetim hicbir sey olcmuyor.")
            return 2
        print("\nPozitif kontroller: 2/2 KIRMIZI (denetim gercekten olcuyor).")

    print()
    print("YERLESIM DENETIMI: " + ("KIRMIZI" if rc else "YESIL"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
