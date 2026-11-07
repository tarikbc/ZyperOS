#!/usr/bin/env python3
"""Where a translation may not use the glossary's word for a term.

    python3 tools/i18n-terms.py [--lang CODE] [TERM...]

For each term in res/strings/GLOSSARY.md's table whose English is in a
string, whether that string's translation has one of the glossary's words
for it (any of the forms between slashes, case and accents as written,
word stems allowed: "jogo" finds "jogos"). A list for a person to read, not
a check: a sentence may say the thing another way on purpose. Standard
library only.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i18n  # noqa: E402

GLOSSARY = os.path.join(i18n.ROOT, "res", "strings", "GLOSSARY.md")


def terms():
    """[(english forms, {code: [forms]})] from the Terms table"""
    out, cols = [], None
    with open(GLOSSARY, encoding="utf-8") as f:
        text = f.read()
    table = text.split("## Terms", 1)[1]
    for line in table.splitlines():
        if not line.startswith("|"):
            if cols:
                break
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if cells[0] == "English":
            names = {n: c for c, n, _, _ in i18n.LANGS}
            cols = [names.get(c) for c in cells]
            continue
        if set(cells[0]) <= set("-: "):
            continue
        en = [re.sub(r"\(.*?\)", "", x).strip() for x in cells[0].split("/")]
        tr = {}
        for code, cell in zip(cols[1:], cells[1:]):
            if code:
                tr[code] = [re.sub(r"\(.*?\)", "", x).strip() for x in re.split(r"[/（）()]", cell) if x.strip()]
        out.append(([e for e in en if e and len(e) > 2], tr))
    return out


def has(text, form):
    form = form.lower().replace("\\u00a0", " ")
    t = text.lower()
    if re.search(r"[　-鿿]", form):
        return form in t
    stem = form if len(form) < 5 else form[:-1]
    return re.search(r"(?<![\w])" + re.escape(stem), t) is not None


def main():
    args = sys.argv[1:]
    langs = [c for c in i18n.CODES if c != "en"]
    if args[:1] == ["--lang"]:
        langs, args = [args[1]], args[2:]
    want = set(a.lower() for a in args)
    errors = []
    strs = i18n.parse(errors)
    n = 0
    for en_forms, tr in terms():
        if want and not (set(e.lower() for e in en_forms) & want):
            continue
        for s in strs:
            en = s.text.get("en")
            en_text = " ".join(en) if isinstance(en, tuple) else (en or "")
            if not any(re.search(r"(?<![\w])" + re.escape(e) + r"(?![\w])", en_text, re.I) for e in en_forms):
                continue
            for code in langs:
                v = s.text.get(code)
                text = " ".join(v) if isinstance(v, tuple) else (v or "")
                forms = tr.get(code, [])
                if forms and not any(has(text, f) for f in forms):
                    print("%-6s %-34s %-18s %r  (glossary: %s)" % (code, s.id, "/".join(en_forms)[:18], text, " / ".join(forms)))
                    n += 1
    print("%d places to look at" % n, file=sys.stderr)


if __name__ == "__main__":
    main()
