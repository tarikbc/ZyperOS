#!/usr/bin/env python3
"""ZyperOS's words: every string a player reads, in every language, in one place.

    python3 tools/i18n.py            write everything below (make strings)
    python3 tools/i18n.py --check    fail if anything below is stale, if a
                                     string lacks a language, if a translation's
                                     printf conversions differ from English's, or
                                     if an ID is used where none is defined or
                                     defined where none is used (make check-strings)
    python3 tools/i18n.py --list ID...   one string's every language, for a look
    python3 tools/i18n.py --widths   the widest language of each string against
                                     English's, in Lexend (needs Pillow)
    python3 tools/i18n.py --export CODE OUT.tsv   every string as a row (ID,
                                     English, note, CODE's), for a translator
    python3 tools/i18n.py --import CODE IN.tsv    CODE's texts (the last
                                     column) back into res/strings' blocks
    python3 tools/i18n.py --lint [CODE...]   the blocks' rules alone, and the
                                     strings that lack CODE: for a copy

The source is res/strings/*.txt, a block a string:

    [menu.language]
    note = A MENU row: the setting that picks the language
    en = Language
    zh-Hans = 语言
    es = Idioma
    pt-BR = Idioma
    fr = Langue

    [home.games]                  a count: one form for 1, another for the rest
    en.one = %d game              (French's one is 0 and 1; the others' is 1)
    en.other = %d games
    zh-Hans = %d 个游戏         (Chinese, which has no plural, has one form)
    ...

A value is the text itself, to the end of the line; "\\n" is a line break,
"\\u00a0" a character by its code (French's no-break space before ! ? : ;),
"\\\\" a backslash, and a value that starts or ends with a space is written in
quotes ("  like this "). The order of the files and of the blocks in them is
the order of the IDs. printf conversions (%s, %d, %.1f, %%) are allowed, and
every language must have English's, in the same order, or %1$s style for
another order.

Writes, from those files alone:

  src/i18n_ids.h      the IDs as an enum (S_MENU_LANGUAGE, ...) and the languages
  src/i18n_tables.c   each language's strings, one blob a language and an
                      offset table, read only, no relocations: T(id) is a load
                      and an add (src/i18n.h)
  res/web/i18n/LANG.js  the File Transfer page's strings (IDs under web.), a
                      file a language that the page loads as it is

The pseudo-locale (code "xx", host builds only, with -DI18N_PSEUDO) is every
string in accented letters and 40% longer, in brackets, so that a screen drawn
in it shows what does not fit before a translation does (make check-i18n-shots).

Where an ID is used decides the check of its use: web.* on the page
(res/web), boot.* by tools/genfirststart.py, everything else in the launcher's
C (src/). Standard library only, but for --widths.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, "res", "strings")
OUT_H = os.path.join(ROOT, "src", "i18n_ids.h")
OUT_C = os.path.join(ROOT, "src", "i18n_tables.c")
OUT_WEB = os.path.join(ROOT, "res", "web", "i18n")

# The languages, in the order the picker lists them: the code the setting
# keeps and theme.txt carries, the name in its own words, the enum's name, and
# which numbers take the "one" form. English is first and is the fallback.
# My five (2026-11-07, docs/design/mock/languages-lab.html): the most
# spoken languages ZyperOS can draw, in the order of how many speak them; and
# the three he added the same day, German, Japanese and Italian, after them
# in the same order. Chinese and Japanese have no plural: their counts are
# one form.
LANGS = [
    ("en", "English", "LANG_EN", "n1"),
    ("zh-Hans", "简体中文", "LANG_ZH_HANS", "none"),
    ("es", "Español", "LANG_ES", "n1"),
    ("pt-BR", "Português (Brasil)", "LANG_PT_BR", "n1"),
    ("fr", "Français", "LANG_FR", "n01"),
    ("de", "Deutsch", "LANG_DE", "n1"),
    ("ja", "日本語", "LANG_JA", "none"),
    ("it", "Italiano", "LANG_IT", "n1"),
]
CODES = [l[0] for l in LANGS]
RULE = {l[0]: l[3] for l in LANGS}
PSEUDO = "xx"

# An area and a name at least, so every enum name has an underscore after
# S_: one without (S_ISDIR, a museart.c enum's S_NEXT) is never this table's
ID_RE = re.compile(r"^[a-z][a-z0-9_]*(\.[a-z0-9_]+)+$")
FMT_RE = re.compile(r"%(?:(\d+)\$)?([-+ #0]*)(\d+|\*)?(?:\.(\d+|\*))?(hh|h|ll|l|z|j|t)?([diouxXeEfgGcspn%])")


class Str:
    __slots__ = ("id", "file", "line", "note", "text", "plural")

    def __init__(self, sid, file, line):
        self.id, self.file, self.line = sid, file, line
        self.note = ""
        self.text = {}        # code -> text, or code -> (one, other) for a plural
        self.plural = False


def unquote(v):
    v = v.strip()
    if len(v) >= 2 and v[0] == '"' and v[-1] == '"':
        v = v[1:-1]
    out, i = [], 0
    while i < len(v):
        c = v[i]
        if c == "\\" and i + 5 < len(v) + 0 and v[i + 1] == "u" and re.match(r"[0-9a-fA-F]{4}$", v[i + 2:i + 6]):
            out.append(chr(int(v[i + 2:i + 6], 16)))
            i += 6
            continue
        if c == "\\" and i + 1 < len(v):
            n = v[i + 1]
            out.append({"n": "\n", "\\": "\\", '"': '"', "t": "\t"}.get(n, "\\" + n))
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def parse(errors):
    """Every block of every file, in order"""
    strs, seen = [], {}
    files = sorted(glob.glob(os.path.join(SRC_DIR, "*.txt")))
    for path in files:
        rel = os.path.relpath(path, ROOT)
        cur = None
        with open(path, encoding="utf-8") as f:
            for n, raw in enumerate(f, 1):
                line = raw.rstrip("\n")
                if not line.strip() or line.lstrip().startswith("#"):
                    continue
                if line.startswith("["):
                    m = re.match(r"^\[([^\]]+)\]\s*$", line)
                    if not m or not ID_RE.match(m.group(1)):
                        errors.append("%s:%d: not an ID: %s" % (rel, n, line))
                        cur = None
                        continue
                    sid = m.group(1)
                    if sid in seen:
                        errors.append("%s:%d: %s again (first at %s)" % (rel, n, sid, seen[sid]))
                    seen[sid] = "%s:%d" % (rel, n)
                    cur = Str(sid, rel, n)
                    strs.append(cur)
                    continue
                if cur is None:
                    errors.append("%s:%d: a line outside a block" % (rel, n))
                    continue
                m = re.match(r"^([A-Za-z-]+)(?:\.(one|other))?\s*=(.*)$", line)
                if not m:
                    errors.append("%s:%d: not key = value: %s" % (rel, n, line))
                    continue
                key, form, val = m.group(1), m.group(2), unquote(m.group(3))
                if key == "note":
                    cur.note = val
                    continue
                if key not in CODES:
                    errors.append("%s:%d: %s: no language %s" % (rel, n, cur.id, key))
                    continue
                if val != val.strip() and not m.group(3).strip().startswith('"'):
                    errors.append("%s:%d: %s %s: spaces at an end; quote it" % (rel, n, cur.id, key))
                if form:
                    cur.plural = True
                    one, other = cur.text.get(key, (None, None)) if isinstance(cur.text.get(key), tuple) else (None, None)
                    cur.text[key] = (val, other) if form == "one" else (one, val)
                else:
                    if key in cur.text:
                        errors.append("%s:%d: %s %s twice" % (rel, n, cur.id, key))
                    cur.text[key] = val
    return strs


def conversions(s):
    """The printf conversions of s, as (position, type) in argument order"""
    out, auto = [], 0
    for m in FMT_RE.finditer(s):
        if m.group(6) == "%":
            continue
        t = (m.group(5) or "") + m.group(6)
        t = {"i": "d"}.get(t, t)
        if m.group(1):
            out.append((int(m.group(1)), t))
        else:
            auto += 1
            out.append((auto, t))
    return sorted(out)


def validate(strs, errors, allow_missing=False):
    for s in strs:
        if "en" not in s.text:
            errors.append("%s:%d: %s has no English" % (s.file, s.line, s.id))
            continue
        for code in CODES:
            v = s.text.get(code)
            if v is None:
                if not allow_missing:
                    errors.append("%s:%d: %s has no %s" % (s.file, s.line, s.id, code))
                continue
            if s.plural and not isinstance(v, tuple) and RULE[code] == "none":
                v = (v, v)
            elif s.plural != isinstance(v, tuple):
                errors.append("%s:%d: %s %s: a count in one language is a count in all" % (s.file, s.line, s.id, code))
                continue
            forms = v if isinstance(v, tuple) else (v,)
            ens = s.text["en"] if isinstance(s.text["en"], tuple) else (s.text["en"],)
            for i, t in enumerate(forms):
                if t is None:
                    errors.append("%s:%d: %s %s lacks its %s form" % (s.file, s.line, s.id, code, ("one", "other")[i]))
                    continue
                if conversions(t) != conversions(ens[min(i, len(ens) - 1)] or ""):
                    errors.append("%s:%d: %s %s: %s has %s, English %s" % (
                        s.file, s.line, s.id, code, repr(t), conversions(t), conversions(ens[min(i, len(ens) - 1)] or "")))


def cname(sid):
    return "S_" + sid.upper().replace(".", "_")


def get(s, code, form=0):
    """The text for code, English where a language lacks one (only ever while
    a translation is being written: --check fails on it)"""
    v = s.text.get(code)
    if v is None:
        v = s.text["en"]
    if isinstance(v, tuple):
        return v[form] if v[form] is not None else s.text["en"][form]
    return v          # one form for both, in a language with no plural


# ---- the pseudo-locale ------------------------------------------------------

ACCENT = dict(zip("abcdeghiklnorstuyzABCDEGHIKLNORSTUYZ",
                  "àbçđéĝĥîķĺñöŕšţûýžÀßÇĐÉĜĤÎĶĹÑÖŘŠŢÛÝŽ"))


def pseudo(s):
    """Accented, 40% longer (at least three characters), in brackets; printf
    conversions and line breaks kept as they are"""
    if not s:
        return s
    parts, pos = [], 0
    for m in FMT_RE.finditer(s):
        parts.append("".join(ACCENT.get(c, c) for c in s[pos:m.start()]))
        parts.append(m.group(0))
        pos = m.end()
    parts.append("".join(ACCENT.get(c, c) for c in s[pos:]))
    body = "".join(parts)
    visible = len(FMT_RE.sub("", s))
    pad = max(3, (visible * 2 + 4) // 5)
    lines = body.split("\n")
    lines[-1] += "·" * pad
    return "[" + "\n".join(lines) + "]"


# ---- writing -----------------------------------------------------------------

HEAD = "/* SPDX-License-Identifier: GPL-3.0-only */\n"


def c_literal(s):
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 0x20:
            out.append("\\%03o" % ord(ch))
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


# The areas the launcher never reads: the page's (res/web/i18n/) and the
# boot screen's pictures' (tools/genfirststart.py). Left out of its tables.
NOT_C = ("web.", "boot.")


def flat(strs):
    """(enum name, string, form) in ID order: a count is two IDs, its one form
    and its other form after it. Only what the launcher reads (NOT_C)."""
    rows = []
    for s in strs:
        if s.id.startswith(NOT_C):
            continue
        if s.plural:
            rows.append((cname(s.id), s, 0))
            rows.append((cname(s.id) + "__OTHER", s, 1))
        else:
            rows.append((cname(s.id), s, 0))
    return rows


def write_h(strs):
    rows = flat(strs)
    o = [HEAD, "/* The string IDs and the languages. GENERATED by tools/i18n.py from\n",
         " * res/strings/: edit those files, run `make strings`. src/i18n.h says how\n",
         " * they are read. */\n",
         "#ifndef ZYPEROS_I18N_IDS_H\n#define ZYPEROS_I18N_IDS_H\n\n",
         "#include <stdint.h>\n\n",
         "typedef enum {\n"]
    for code, name, enum, _ in LANGS:
        o.append("\t%s,\t/* %s, %s */\n" % (enum, code, name))
    o.append("\tLANG_COUNT\n} lang;\n\n")
    o.append("/* The pseudo-locale, xx: host builds with -DI18N_PSEUDO only */\n")
    o.append("#define LANG_PSEUDO LANG_COUNT\n\n")
    o.append("typedef enum {\n")
    last = None
    for name, s, form in rows:
        if s.file != last:
            o.append("\t/* %s */\n" % s.file)
            last = s.file
        o.append("\t%s,\n" % name)
    o.append("\tS_COUNT\n} str_id;\n\n")
    o.append("/* Whether a language's one form is 0 as well as 1 (French) */\n")
    o.append("#define I18N_PLURAL_RULES { %s }\n\n" % ", ".join(
        {"n1": "0", "n01": "1", "none": "0"}[l[3]] for l in LANGS))
    o.append("#endif\n")
    return "".join(o)


def blob(texts):
    """One C string of every text, NUL after each, and the offsets"""
    lits, offs, at = [], [], 0
    for t in texts:
        b = t.encode("utf-8")
        offs.append(at)
        at += len(b) + 1
        lits.append("\t" + c_literal(t)[:-1] + '\\0"')
    return lits, offs, at


def write_c(strs):
    rows = flat(strs)
    o = [HEAD, "/* Every language's strings. GENERATED by tools/i18n.py from\n",
         " * res/strings/: edit those files, run `make strings`.\n",
         " *\n * One blob a language, its strings one after another with a NUL after\n",
         " * each, and where each starts: read only and with no pointers, so it costs\n",
         " * the loader nothing and a language's pages are only read in once used. */\n",
         '#include "i18n_ids.h"\n\n']
    langs = [(l[0], l[2]) for l in LANGS] + [(PSEUDO, "LANG_PSEUDO")]
    for code, enum in langs:
        if code == PSEUDO:
            o.append("#ifdef I18N_PSEUDO\n")
            texts = [pseudo(get(s, "en", form)) for _, s, form in rows]
        else:
            texts = [get(s, code, form) for _, s, form in rows]
        lits, offs, size = blob(texts)
        ident = code.replace("-", "_").lower()
        o.append("/* %s: %d bytes */\n" % (code, size))
        o.append("static const char blob_%s[%d] =\n%s;\n" % (ident, size, "\n".join(lits)))
        o.append("static const uint32_t off_%s[S_COUNT] = {\n" % ident)
        for i in range(0, len(offs), 10):
            o.append("\t" + ", ".join(str(x) for x in offs[i:i + 10]) + ",\n")
        o.append("};\n")
        if code == PSEUDO:
            o.append("#endif\n")
        o.append("\n")
    o.append("#ifdef I18N_PSEUDO\n#define I18N_N (LANG_COUNT + 1)\n#else\n#define I18N_N LANG_COUNT\n#endif\n\n")
    o.append("const char *const i18n_blobs[I18N_N] = {\n")
    for code, enum in langs:
        ident = code.replace("-", "_").lower()
        if code == PSEUDO:
            o.append("#ifdef I18N_PSEUDO\n")
        o.append("\t[%s] = blob_%s,\n" % (enum, ident))
        if code == PSEUDO:
            o.append("#endif\n")
    o.append("};\nconst uint32_t *const i18n_offs[I18N_N] = {\n")
    for code, enum in langs:
        ident = code.replace("-", "_").lower()
        if code == PSEUDO:
            o.append("#ifdef I18N_PSEUDO\n")
        o.append("\t[%s] = off_%s,\n" % (enum, ident))
        if code == PSEUDO:
            o.append("#endif\n")
    o.append("};\n")
    o.append("const char *const i18n_codes[LANG_COUNT] = { %s };\n" % ", ".join(c_literal(l[0]) for l in LANGS))
    o.append("const char *const i18n_names[LANG_COUNT] = { %s };\n" % ", ".join(c_literal(l[1]) for l in LANGS))
    return "".join(o)


def js_string(s):
    out = []
    for ch in s:
        if ch in "\\\"":
            out.append("\\" + ch)
        elif ch == "\n":
            out.append("\\n")
        elif ord(ch) < 0x20 or ch in "  ":
            out.append("\\u%04x" % ord(ch))
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def write_web(strs):
    """LANG.js a language: window.ZYPEROS_STRINGS, the page's strings by ID
    without the web. prefix; a count is [one, other]"""
    out = {}
    web = [s for s in strs if s.id.startswith("web.")]
    for code, name, _, rule in LANGS:
        lines = ["/* %s. GENERATED by tools/i18n.py from res/strings: edit those. */" % name,
                 "window.ZYPEROS_LANG = %s;" % js_string(code),
                 "window.ZYPEROS_PLURAL = %s;" % {"n01": "n => n > 1 ? 1 : 0", "n1": "n => n === 1 ? 0 : 1", "none": "n => 1"}[rule],
                 "window.ZYPEROS_STRINGS = {"]
        for s in web:
            key = s.id[4:]
            if s.plural:
                lines.append("  %s: [%s, %s]," % (js_string(key), js_string(get(s, code, 0)), js_string(get(s, code, 1))))
            else:
                lines.append("  %s: %s," % (js_string(key), js_string(get(s, code))))
        lines.append("};")
        out[os.path.join(OUT_WEB, code + ".js")] = "\n".join(lines) + "\n"
    return out


def outputs(strs):
    o = {OUT_H: write_h(strs), OUT_C: write_c(strs)}
    o.update(write_web(strs))
    return o


# ---- where IDs are used --------------------------------------------------------

def uses():
    """{id: [where]} for the C enum names in src/, the page's keys in res/web,
    and boot.* in tools/genfirststart.py"""
    c, web = {}, {}
    for path in glob.glob(os.path.join(ROOT, "src", "*.[ch]")) + glob.glob(os.path.join(ROOT, "src", "device", "*.[ch]")):
        if os.path.basename(path).startswith("i18n"):
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            for n, line in enumerate(f, 1):
                for m in re.finditer(r"\bS_[A-Z0-9_]+\b", line):
                    c.setdefault(m.group(0), []).append("%s:%d" % (os.path.relpath(path, ROOT), n))
    for path in glob.glob(os.path.join(ROOT, "res", "web", "*.js")) + glob.glob(os.path.join(ROOT, "res", "web", "*.html")):
        with open(path, encoding="utf-8") as f:
            for n, line in enumerate(f, 1):
                for m in re.finditer(r"""(?:\bt|\btn)\(\s*["']([a-z0-9_.]+)["']|data-t(?:-[a-z]+)?=["']([a-z0-9_.]+)["']""", line):
                    web.setdefault("web." + (m.group(1) or m.group(2)), []).append("%s:%d" % (os.path.relpath(path, ROOT), n))
    return c, web


def font_chars(path):
    """The characters a TrueType or OpenType font maps (its cmap's Unicode
    subtables, formats 4 and 12), standard library only"""
    import struct
    with open(path, "rb") as f:
        data = f.read()
    n = struct.unpack(">H", data[4:6])[0]
    cmap = None
    for i in range(n):
        tag, _, off, _ = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
        if tag == b"cmap":
            cmap = off
    chars = set()
    if cmap is None:
        return chars
    count = struct.unpack(">H", data[cmap + 2:cmap + 4])[0]
    for i in range(count):
        pid, eid, off = struct.unpack(">HHI", data[cmap + 4 + 8 * i:cmap + 12 + 8 * i])
        if (pid, eid) not in ((3, 1), (3, 10), (0, 3), (0, 4)):
            continue
        t = cmap + off
        fmt = struct.unpack(">H", data[t:t + 2])[0]
        if fmt == 4:
            segx2 = struct.unpack(">H", data[t + 6:t + 8])[0]
            ends = struct.unpack(">%dH" % (segx2 // 2), data[t + 14:t + 14 + segx2])
            starts = struct.unpack(">%dH" % (segx2 // 2), data[t + 16 + segx2:t + 16 + 2 * segx2])
            deltas = struct.unpack(">%dh" % (segx2 // 2), data[t + 16 + 2 * segx2:t + 16 + 3 * segx2])
            ro_at = t + 16 + 3 * segx2
            ros = struct.unpack(">%dH" % (segx2 // 2), data[ro_at:ro_at + segx2])
            for k, (a, b) in enumerate(zip(starts, ends)):
                for c in range(a, b + 1):
                    if c == 0xFFFF:
                        continue
                    if ros[k] == 0:
                        g = (c + deltas[k]) & 0xFFFF
                    else:
                        at = ro_at + 2 * k + ros[k] + 2 * (c - a)
                        g = struct.unpack(">H", data[at:at + 2])[0]
                        g = (g + deltas[k]) & 0xFFFF if g else 0
                    if g:
                        chars.add(chr(c))
        elif fmt == 12:
            ngroups = struct.unpack(">I", data[t + 12:t + 16])[0]
            for k in range(ngroups):
                a, b, g = struct.unpack(">III", data[t + 16 + 12 * k:t + 28 + 12 * k])
                chars.update(chr(c) for c in range(a, b + 1))
    return chars


# Which fonts draw which language: Lexend everything but Chinese and
# Japanese, which are drawn in tools/cjkfont.py's cuts of Noto Sans SC and
# Noto Sans JP (the two write some of the same characters differently)
FONTS = {"zh-Hans": ("zh-menu.otf", "zh-title.otf"), "ja": ("ja-menu.otf", "ja-title.otf")}
LATIN_FONTS = ("menu.ttf", "title.ttf")


def check_glyphs(strs, errors):
    """Every character of every language in the fonts that draw it, and every
    language's own name in the font of the language it is (the picker draws
    each in its own)"""
    have = {}
    for name in LATIN_FONTS + tuple(x for v in FONTS.values() for x in v):
        path = os.path.join(ROOT, "res", "fonts", name)
        have[name] = font_chars(path) if os.path.exists(path) else None
    for code in CODES:
        for name in FONTS.get(code, LATIN_FONTS):
            if have[name] is None:
                errors.append("res/fonts/%s is missing: run python3 tools/cjkfont.py" % name)
                continue
            texts = [lname for c, lname, _, _ in LANGS if c == code]
            for s in strs:
                v = s.text.get(code)
                texts += list(v) if isinstance(v, tuple) else [v or ""]
            if code == "en" and name in LATIN_FONTS:
                texts += [pseudo(t) for t in texts if t]
            missing = sorted({ch for t in texts for ch in (t or "") if ch not in have[name] and ch != "\n"})
            if missing:
                errors.append("res/fonts/%s lacks %s for %s%s" % (
                    name, " ".join("U+%04X %s" % (ord(c), c) for c in missing[:12]), code,
                    "; run python3 tools/cjkfont.py" if code in FONTS else ""))


def check_uses(strs, errors):
    c, web = uses()
    names = {}
    for s in strs:
        if s.id.startswith(NOT_C):
            continue
        names[cname(s.id)] = s
        if s.plural:
            names[cname(s.id) + "__OTHER"] = s
    ids = {s.id for s in strs}
    for s in strs:
        if s.id.startswith("web."):
            if s.id not in web:
                errors.append("%s:%d: %s is used nowhere on the page" % (s.file, s.line, s.id))
        elif s.id.startswith("boot."):
            continue
        elif cname(s.id) not in c:
            errors.append("%s:%d: %s (%s) is used nowhere in src/" % (s.file, s.line, s.id, cname(s.id)))
    for name, where in c.items():
        if name not in names and name != "S_COUNT" and "_" in name[2:]:
            errors.append("%s: %s is no string in res/strings" % (where[0], name))
    for sid, where in web.items():
        if sid not in ids:
            errors.append("%s: %s is no string in res/strings" % (where[0], sid))


# ---- a language as a table, out and back in -----------------------------------

def tsv_escape(t):
    return (t or "").replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def tsv_unescape(t):
    out, i = [], 0
    while i < len(t):
        if t[i] == "\\" and i + 1 < len(t):
            out.append({"n": "\n", "t": "\t", "\\": "\\"}.get(t[i + 1], "\\" + t[i + 1]))
            i += 2
        else:
            out.append(t[i])
            i += 1
    return "".join(out)


def export(strs, code, path):
    """Every string as a row: ID (a count's two forms as ID.one and
    ID.other), English, its note, and `code`'s text where it has one"""
    rows = ["id\ten\tnote\t" + code]
    for s in strs:
        en, tr = s.text.get("en"), s.text.get(code)
        if s.plural:
            for i, form in enumerate(("one", "other")):
                t = tr[i] if isinstance(tr, tuple) else (tr or "")
                rows.append("\t".join((s.id + "." + form, tsv_escape(en[i]), tsv_escape(s.note), tsv_escape(t or ""))))
        else:
            rows.append("\t".join((s.id, tsv_escape(en), tsv_escape(s.note), tsv_escape(tr or ""))))
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(rows) + "\n")
    print("%s: %d rows" % (path, len(rows) - 1))


def value_text(t):
    """A value as the source writes it: escapes for a line break, a
    backslash and the no-break spaces, quotes when it starts or ends with a
    space"""
    v = (t.replace("\\", "\\\\").replace("\n", "\\n")
         .replace("\u00a0", "\\u00a0").replace("\u202f", "\\u202f"))
    return '"%s"' % v if v != v.strip() else v


def import_(code, path):
    """`code`'s texts from a table export wrote (its last column), into the
    blocks of res/strings: a line that is there replaced, a new one put
    after the languages before it in LANGS order. A blank cell changes
    nothing; a count's forms with one/other, or one form in a language
    without a plural."""
    want = {}
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f):
            cols = line.rstrip("\n").split("\t")
            if n == 0 or len(cols) < 2:
                continue
            text = tsv_unescape(cols[-1])
            if text.strip():
                want[cols[0]] = text
    order = {c: i for i, c in enumerate(CODES)}
    me = order[code]
    changed = 0
    for path_txt in sorted(glob.glob(os.path.join(SRC_DIR, "*.txt"))):
        with open(path_txt, encoding="utf-8") as f:
            lines = f.read().split("\n")
        out, i, dirty = [], 0, False
        while i < len(lines):
            m = re.match(r"^\[([^\]]+)\]\s*$", lines[i])
            if not m:
                out.append(lines[i])
                i += 1
                continue
            sid = m.group(1)
            j = i + 1
            while j < len(lines) and not lines[j].startswith("["):
                j += 1
            block = lines[i:j]
            while block and not block[-1].strip():
                block.pop()
            tail = lines[i + len(block):j]
            plural = any(re.match(r"^en\.(one|other)\s*=", b) for b in block)
            new = []
            if plural and RULE[code] != "none":
                for form in ("one", "other"):
                    if sid + "." + form in want:
                        new.append("%s.%s = %s" % (code, form, value_text(want[sid + "." + form])))
            elif plural:
                t = want.get(sid + ".other") or want.get(sid + ".one") or want.get(sid)
                if t:
                    new.append("%s = %s" % (code, value_text(t)))
            elif sid in want:
                new.append("%s = %s" % (code, value_text(want[sid])))
            if new:
                kept = [b for b in block if not re.match(r"^%s(\.(one|other))?\s*=" % re.escape(code), b)]
                at = 1
                for k, b in enumerate(kept):
                    mm = re.match(r"^([A-Za-z-]+)(?:\.(one|other))?\s*=", b)
                    if mm and mm.group(1) in order and order[mm.group(1)] < me:
                        at = k + 1
                block = kept[:at] + new + kept[at:]
                if block != lines[i:i + len(block)]:
                    dirty = True
                changed += 1
            out.extend(block)
            out.extend(tail)
            i = j
        if dirty:
            with open(path_txt, "w", encoding="utf-8") as f:
                f.write("\n".join(out))
    print("%s: %d strings into res/strings" % (code, changed))


def main():
    args = sys.argv[1:]
    errors = []
    strs = parse(errors)
    if args[:1] == ["--export"] and len(args) == 3:
        export(strs, args[1], args[2])
        return 0
    if args[:1] == ["--lint"]:
        # The blocks' own rules (conversions, a count's forms), and which
        # strings lack the languages named, if any: for a translator's copy
        validate(strs, errors, allow_missing=True)
        for code in args[1:]:
            for s in strs:
                if code not in s.text:
                    errors.append("%s:%d: %s has no %s" % (s.file, s.line, s.id, code))
        for e in errors:
            print("i18n: " + e, file=sys.stderr)
        print("lint: %d strings, %d problems" % (len(strs), len(errors)))
        return 1 if errors else 0
    if args[:1] == ["--import"] and len(args) == 3:
        if args[1] not in CODES:
            raise SystemExit("i18n: no language " + args[1])
        import_(args[1], args[2])
        return 0
    if args[:1] == ["--list"]:
        want = set(args[1:])
        for s in strs:
            if s.id in want:
                print("[%s]  %s:%d" % (s.id, s.file, s.line))
                for code in CODES:
                    print("  %-6s %r" % (code, s.text.get(code)))
        return 0
    if args[:1] == ["--widths"]:
        return widths(strs)
    check = args[:1] == ["--check"]
    validate(strs, errors, allow_missing=not check and "--strict" not in args)
    if check:
        check_uses(strs, errors)
        check_glyphs(strs, errors)
    if errors:
        for e in errors:
            print("i18n: " + e, file=sys.stderr)
        if check or "--strict" in args:
            print("i18n: %d problems" % len(errors), file=sys.stderr)
            return 1
    outs = outputs(strs)
    stale = []
    for path, text in outs.items():
        old = None
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                old = f.read()
        if old == text:
            continue
        if check:
            stale.append(os.path.relpath(path, ROOT))
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            print("wrote " + os.path.relpath(path, ROOT))
    if check:
        for path in glob.glob(os.path.join(OUT_WEB, "*.js")):
            if path not in outs:
                stale.append(os.path.relpath(path, ROOT) + " (no such language)")
        if stale:
            for p in stale:
                print("i18n: stale %s: run python3 tools/i18n.py" % p, file=sys.stderr)
            return 1
        n = sum(2 if s.plural else 1 for s in strs)
        print("check-strings: %d strings (%d IDs) in %d languages, every one complete and used" % (len(strs), n, len(LANGS)))
    return 0


def widths(strs):
    """Each string's widest translation against its English, in the UI font at
    the menu size: where a layout made for English is most likely to overflow"""
    from PIL import ImageFont
    f = ImageFont.truetype(os.path.join(ROOT, "res", "fonts", "menu.ttf"), 49)
    rows = []
    for s in strs:
        en = get(s, "en", 1 if s.plural else 0)
        if not en.strip():
            continue
        w0 = max(f.getlength(x) for x in en.split("\n"))
        best = max(((max(f.getlength(x) for x in get(s, c, 1 if s.plural else 0).split("\n")), c) for c in CODES[1:]), default=(w0, "en"))
        rows.append((best[0] / max(w0, 1), s.id, best[1], int(w0), int(best[0])))
    for r, sid, code, w0, w1 in sorted(rows, reverse=True)[:60]:
        print("%5.2f  %-40s %-6s %4d -> %4d" % (r, sid, code, w0, w1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
