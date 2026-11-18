"""The services' reference pages (docs/modding/reference/services), made from the build.

    python -X utf8 -I tools/modding/service_pages.py --forms <file> [--sdk <folder>] [--pages <folder>] [--check]

--forms: what `lumalink services --sdk <SDK> --forms` prints, the LumaScript form of every function as the compiler
gives it (a `form` line each, `-` for one a script does not call by name, and a `wait` line for a request a task may
wait). --sdk: the SDK's headers (default native/include). --check writes nothing: it prints how each page differs from
what the build makes and exits 1 when one does.

A page made from one header (its first table a row per service: version, permission, C import, LumaScript) is made
whole from that header but for its frontmatter, its About text and any text after the header's own description, which
are kept as written: the header's description, its limits and constants, types, enumerations and structures with
their comments, and each function of each service table in the header's order, its LumaScript form (the compiler's),
its C declaration, the version that brought it, its comment and its events.

A page written by hand (fs, events, mods, save, system, time: no such table) keeps its text; its code is the build's:
each `### <fn>` section of a `## <service> functions` part has its C block replaced by the header's member and its
LumaScript block by the compiler's form, and a function of the service's table without a section, or a section of no
function, is a difference.
"""
import argparse
import difflib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# ---- The headers ---------------------------------------------------------------------------------------------------

# A reference to another document in a header comment ("(see docs/...)", "(registry 4)"): not for the page.
REF = re.compile(r"\s*\((?:see )?(?:docs/|services-registry|registry \d|references \d|content/|services-|luma-format|"
                 r"harness \d|language |D\d|L\d)[^()]*(?:\([^()]*\)[^()]*)*\)")


def clean(t):
    t = re.sub(r"\s+", " ", t or "").strip()
    t = REF.sub("", t)
    # A design document named inside a sentence (": content/animation.md").
    t = re.sub(r"[:,;]? ?\b(?:content|services|docs|design)/[\w./-]+\.md\b", "", t)
    # The import a header's description names: the page's table gives it from the header's version (M-176).
    t = re.sub(r" ?Import: `?LUMA_SERVICE\([^)]*\)`?(?: (?:or|and) `?LUMA_SERVICE\([^)]*\)`?)*\.?", "", t)
    # A space left before punctuation by a removed reference, but not one before a file's extension (".bpk").
    t = re.sub(r" ([,.;:])(?=\s|$)", r"\1", t)
    t = re.sub(r" ?\(\s*\)", "", t)
    return t.strip()


def tokens(src):
    """The header as ('comment', text), ('code', line) and ('nl', '') items."""
    i, n, out, buf = 0, len(src), [], ""
    while i < n:
        if src.startswith("/*", i):
            j = src.index("*/", i) + 2
            if buf.strip():
                out.append(("code", buf))
                buf = ""
            out.append(("comment", src[i + 2:j - 2]))
            i = j
        elif src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        else:
            c = src[i]
            if c == "\n":
                if buf.strip():
                    out.append(("code", buf))
                buf = ""
                out.append(("nl", ""))
            else:
                buf += c
            i += 1
    if buf.strip():
        out.append(("code", buf))
    return out


def camel_to_upper(s):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", s).upper()


def parse(path):
    """A header's description (its second comment: the first is the licence), defines, handle and callback types,
    enums, structs and service tables, each with the comments before it and on its line."""
    items = tokens(path.read_text(encoding="utf-8"))
    res = {"top": "", "defines": [], "enums": [], "structs": [], "handles": [], "services": []}
    comments = [t for k, t in items if k == "comment"]
    if len(comments) > 1:
        res["top"] = clean(comments[1])
    pending = []
    k = 0
    while k < len(items):
        kind, t = items[k]
        if kind == "comment":
            pending.append(t)
            k += 1
            continue
        if kind == "nl":
            k += 1
            continue
        line = t.strip()

        def trailing():
            if k + 1 < len(items) and items[k + 1][0] == "comment":
                return items[k + 1][1], 2
            return None, 1
        m = re.match(r"#define\s+(LUMA_\w+)\s+(.+)$", line)
        if m:
            tc, step = trailing()
            res["defines"].append((m.group(1), m.group(2).strip(), clean(tc or " ".join(pending))))
            pending = []
            k += step
            continue
        m = re.match(r"typedef\s+(?:LumaHandle|void\*|const char\*|uint\w+)\s+(Luma\w+);", line)
        if m:
            tc, step = trailing()
            res["handles"].append((m.group(1), line, clean(" ".join(pending) + " " + (tc or ""))))
            pending = []
            k += step
            continue
        m = re.match(r"typedef\s+(?:int32_t|void)\s*\(\*(Luma\w+)\)\s*\((.*)", line)
        if m:
            code, kk = line, k
            while ");" not in code:
                kk += 1
                if items[kk][0] == "code":
                    code += " " + items[kk][1].strip()
            res["handles"].append((m.group(1), re.sub(r"\s+", " ", code), clean(" ".join(pending))))
            pending = []
            k = kk + 1
            continue
        m = re.match(r"(typedef\s+)?(enum|struct)\s*(\w*)\s*\{", line)
        if m:
            what, name = m.group(2), m.group(3)
            pre = clean(" ".join(pending))
            pending = []
            members, mp, tc, tname = [], [], None, name
            k += 1
            code_acc = ""
            while True:
                kind2, t2 = items[k]
                if kind2 == "comment":
                    if code_acc:
                        members[-1][2] = (members[-1][2] + " " + t2) if members[-1][2] else t2
                    else:
                        mp.append(t2)
                    k += 1
                    continue
                if kind2 == "nl":
                    if not (code_acc and (code_acc.count("(") != code_acc.count(")") or
                                          not code_acc.rstrip().endswith((",", ";")))):
                        code_acc = ""
                    k += 1
                    continue
                l2 = t2.strip()
                mc = re.match(r"\}\s*(\w*)\s*;", l2)
                if mc:
                    if k + 1 < len(items) and items[k + 1][0] == "comment":
                        tc = items[k + 1][1]
                        k += 1
                    tname = mc.group(1) or name
                    k += 1
                    break
                if code_acc and (code_acc.count("(") != code_acc.count(")") or not code_acc.rstrip().endswith((",", ";"))):
                    code_acc += " " + l2
                    members[-1][0] = code_acc
                    k += 1
                    continue
                code_acc = l2
                members.append([l2, clean(" ".join(mp)), ""])
                mp = []
                k += 1
            for mm in members:
                mm[2] = clean(mm[2])
            if what == "enum":
                res["enums"].append({"name": tname, "pre": pre, "cases": members, "after": clean(tc or "")})
            elif "Service" in (tname or "") and any(x[0].startswith("LumaServiceTable") for x in members):
                res["services"].append({"name": tname, "pre": pre, "fns": members})
            else:
                res["structs"].append({"name": tname, "pre": pre, "fields": members, "after": clean(tc or "")})
            continue
        pending = []
        k += 1
    return res


def services_of(r):
    """(table, name, version, permission) of each service table of a parsed header."""
    defs = {d[0]: d for d in r["defines"]}
    out = []
    for s in r["services"]:
        base = camel_to_upper(s["name"][4:-7])
        nm = defs.get("LUMA_%s_NAME" % base)
        name = nm[1].strip('"') if nm else base.lower()
        major = defs.get("LUMA_%s_MAJOR" % base, (0, "1u"))[1].rstrip("u")
        minor = defs.get("LUMA_%s_MINOR" % base, (0, "0u"))[1].rstrip("u")
        perm = defs.get("LUMA_%s_PERMISSION" % base)
        out.append((s, name, "%s.%s" % (major, minor), perm[1].strip('"') if perm else None))
    return out


def fn_parts(code):
    """(name, result, parameters) of a table member `ret (*name)(params);`."""
    code = re.sub(r"\s+", " ", code).strip()
    m = re.match(r"(.+?)\(\*(\w+)\)\s*\((.*)\);", code)
    return (m.group(2), m.group(1).strip(), m.group(3)) if m else None


# ---- Prose -------------------------------------------------------------------------------------------------------

US = [("centred", "centered"), ("centre", "center"), ("centimetre", "centimeter"), ("metre", "meter"), ("colour", "color"), ("behaviour", "behavior"),
      ("licence", "license"), ("cancelled", "canceled"), ("analyse", "analyze"), ("normalise", "normalize"),
      ("initialise", "initialize"), ("favour", "favor"), ("grey", "gray")]


def us_spelling(t):
    """The docs' US spelling, outside code spans (`LUMA_E_CANCELLED` stays as the header names it)."""
    parts = t.split("`")
    for i in range(0, len(parts), 2):
        for a, b in US:
            parts[i] = re.sub(r"\b%s" % a, b, parts[i])
            parts[i] = re.sub(r"\b%s" % a.capitalize(), b.capitalize(), parts[i])
    return "`".join(parts)


def bt(m):
    return "`" + m.group(0) + "`"


def sentence(t):
    """A header fragment as prose: identifiers in code font, a capital first letter, a full stop."""
    t = (t or "").strip()
    if not t:
        return ""
    mv0 = re.match(r"(\d\.\d+): ", t)
    if mv0:
        rest = t[mv0.end():]
        t = "Needs service version %s or later. " % mv0.group(1) + (rest[:1].upper() + rest[1:])
    parts = t.split("`")
    for i in range(0, len(parts), 2):
        p = parts[i]
        p = re.sub(r"\bLUMA_[A-Z0-9_]+\b(?:\([^)]*\))?", bt, p)
        p = re.sub(r"(?<![\w`])\*out\b", "`*out`", p)
        p = re.sub(r"\b(Luma(?!Script\b|Link\b|Kit\b|Core\b|Core's\b)[A-Z]\w+)\b", bt, p)
        p = re.sub(r"\b(sizeof|offsetof)\(([^()]*)\)", r"`\1(\2)`", p, flags=re.I)
        # A mod log message's name (Content.MissingAnimation).
        p = re.sub(r"\b([A-Z][a-z]+(?:\.[A-Z][A-Za-z]+)+)\b", r"`\1`", p)
        parts[i] = p
    t = "`".join(parts).replace("``", "`")
    t = re.sub(r"`(sizeof|offsetof)\(`(\w+)`\)`", r"`\1(\2)`", t, flags=re.I)
    for a, b in [("Game thread. ", "Call it on the game thread. "), ("Any thread. ", "You can call it on any thread. "),
                 ("`LUMA_OK` and `*out`.", "It returns `LUMA_OK` and sets `*out`."),
                 ("`LUMA_OK` and `*out`;", "It returns `LUMA_OK` and sets `*out`;"),
                 ("`LUMA_PENDING` and `*out`:", "It returns `LUMA_PENDING` and sets `*out`:"), (", reported", " (reported)"),
                 (" a 2nd:", " a second:"), (" 1st ", " first ")]:
        t = t.replace(a, b)
    t = t.replace("(reported)(reported)", "(reported)")
    if t[0].islower():
        t = t[0].upper() + t[1:]
    if not t.endswith((".", ":", "!", "?", ")", "`")):
        t += "."
    elif t.endswith(")") or t.endswith("`"):
        t += "."
    return us_spelling(t)


def esc(s):
    return s.replace("|", "\\|")


# ---- The forms ---------------------------------------------------------------------------------------------------

def read_forms(path):
    forms, waits = {}, {}
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        parts = line.split("\t")
        if len(parts) != 4:
            continue
        kind, service, fn, form = parts
        if kind == "form":
            forms[(service, fn)] = None if form == "-" else form
        elif kind == "wait":
            waits[(service, fn)] = form
    return forms, waits


def no_form_note(service, name, text):
    """Why a function has no LumaScript form, from what its comment says it is."""
    if service == "draw.native":
        return "C only. Scripts cannot call this function."
    if re.search(r"[Ii]nside (?:the |a |an |each )?[A-Z_]+ (?:event'?s? )?call|inside this call", text) or name.startswith("take"):
        return "Not called by name in LumaScript. Inside the event's closure, the event's `data` already holds what this function takes."
    if service == "time" and name.startswith("on_"):
        return "Not called in LumaScript. Write an `on %s` handler instead." % name[3:]
    return "Not callable from LumaScript."


# ---- A page made from one header ---------------------------------------------------------------------------------

VER = re.compile(r"^(\d\.\d+)[.:]\s*")


def render_fn(fn, service, forms, waits, seen):
    code, pre, trail = fn
    p = fn_parts(code)
    if not p:
        return ""
    name, ret, params = p
    text = pre + (" " + trail if trail else "")
    events = ""
    me = re.search(r"\bevents?: ([A-Z_]+\([^)]*\)(?:, [A-Z_]+\([^)]*\))*)", text)
    if me:
        events = me.group(1)
        text = (text[:me.start()] + text[me.end():]).strip()
    ver = ""
    mv = VER.match(text)
    if mv:
        ver = mv.group(1)
        text = text[mv.end():]
    # The event a request's `wait` gives: its own comment's, or that of the function it is "As".
    said = text
    ma = re.match(r"(?:(?:Game|Any) thread\. )?As (\w+)[,.]", text)
    if ma and ma.group(1) in seen:
        said = seen[ma.group(1)]
    elif text:
        seen[name] = text
    form, wait = forms.get((service, name)), waits.get((service, name))
    out = ["### `%s`" % name, ""]
    if form:
        out += ["```lumascript", form] + ([wait] if wait else []) + ["```", ""]
    out += ["```c", "%s %s(%s);" % (ret, name, params), "```", ""]
    if ver and ver != "1.0":
        out += ["Needs `%s` %s or later." % (service, ver), ""]
    if not form:
        out += [no_form_note(service, name, said), ""]
    if text:
        out += [sentence(text), ""]
    if wait:
        kind = re.search(r"([A-Z_]+) event follows", said)
        out += ["In a task, `wait` gives the %s result instead of calling a closure." %
                ("`%s` event's" % kind.group(1) if kind else "event's"), ""]
    if events:
        out += ["Events: " + ", ".join("`%s`" % e for e in re.findall(r"[A-Z_]+\([^)]*\)", events)) + ".", ""]
    return "\n".join(out)


def kept_parts(text):
    """What a made page keeps as written: its frontmatter, its About text (up to the service table) and the text after
    the header's description (up to the next part)."""
    lines = text.split("\n")
    end = lines.index("---", 1)
    front = "\n".join(lines[:end + 1])
    i = next(n for n, l in enumerate(lines) if l.startswith("## About the "))
    about_heading = lines[i]
    j = next(n for n, l in enumerate(lines) if l.startswith("| Service | Version |"))
    about = "\n".join(lines[i + 1:j]).strip()
    k = next(n for n, l in enumerate(lines) if l.startswith("The header is `native/include/"))
    m = k + 1
    if any(l == "### Details from the header" for l in lines[m:m + 3]):
        m = next(n for n in range(m, len(lines)) if lines[n] == "### Details from the header") + 1
        while m < len(lines) and lines[m] == "":
            m += 1
        m += 1  # the description's paragraph
    e = next((n for n in range(m, len(lines)) if lines[n].startswith("## ")), len(lines))
    extra = "\n".join(lines[m:e]).strip()
    return front, about_heading, about, extra


def made_page(text, header_path, forms, waits):
    front, about_heading, about, extra = kept_parts(text)
    r = parse(header_path)
    services = services_of(r)
    L = [front, "", about_heading, "", about, ""]
    L += ["| Service | Version | Permission | C import | LumaScript |", "|---|---|---|---|---|"]
    for s, name, ver, perm in services:
        L.append('| `%s` | %s | %s | `LUMA_SERVICE(slot, "%s", "%s")` | `use svc.%s` |' %
                 (name, ver, "`%s`" % perm if perm else "None", name, ver, name))
    first = services[0][1]
    L += ["", "The header is `native/include/%s`. In LumaScript, a service is called by the last part of its name, such as "
          "`%s.` for `%s`. Each function below shows its LumaScript form first and its C form second. For how the forms "
          "are made, see [Calling services](calling-services.md). A C mod may import an earlier minor version when it calls "
          "only what that version has: each function says the version it needs when it is not 1.0." %
          (header_path.name, first.split(".")[-1], first), ""]
    if r["top"]:
        L += ["### Details from the header", "", sentence(r["top"]), ""]
    if extra:
        L += [extra, ""]
    lim = [d for d in r["defines"] if not re.search(r"_(NAME|MAJOR|MINOR|PERMISSION)$", d[0])]
    if lim:
        L += ["## Limits and constants", "", "| Name | Value | Description |", "|---|---|---|"]
        for n, v, c in lim:
            L.append("| `%s` | `%s` | %s |" % (n, esc(re.sub(r"(\d)u\b", r"\1", v)), esc(sentence(c))))
        L.append("")
    if r["handles"]:
        L += ["## Types", "", "| Name | Declaration | Description |", "|---|---|---|"]
        for n, code, c in r["handles"]:
            L.append("| `%s` | `%s` | %s |" % (n, esc(code), esc(sentence(c))))
        L.append("")
    if r["enums"]:
        L += ["## Enumerations", ""]
        for e in r["enums"]:
            L += ["### %s" % ("`%s`" % e["name"] if e["name"] else "Other constants"), ""]
            if e["pre"]:
                L += [sentence(e["pre"]), ""]
            L += ["| Name | Description |", "|---|---|"]
            for code, pre, trail in e["cases"]:
                L.append("| `%s` | %s |" % (esc(code.rstrip(",").strip()), esc(sentence((pre + " " + trail).strip()))))
            L.append("")
    if r["structs"]:
        L += ["## Structures", ""]
        for st in r["structs"]:
            L += ["### `%s`" % st["name"], ""]
            if st["pre"]:
                L += [sentence(st["pre"]), ""]
            if st["after"]:
                m = re.match(r"(\d\.\d+): (\d+) bytes", st["after"])
                L += ["Its size in version %s is %s bytes." % (m.group(1), m.group(2)) if m else sentence(st["after"]), ""]
            L += ["| Name | Description |", "|---|---|"]
            for code, pre, trail in st["fields"]:
                L.append("| `%s` | %s |" % (esc(code.rstrip(";").strip()), esc(sentence((pre + " " + trail).strip()))))
            L.append("")
    for s, name, ver, perm in services:
        L += ["## `%s` functions" % name, ""]
        if s["pre"]:
            L += [sentence(s["pre"]), ""]
        L += ["In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.", ""]
        seen = {}
        for fn in s["fns"]:
            if not fn[0].startswith("LumaServiceTable"):
                L.append(render_fn(fn, name, forms, waits, seen))
    L += ["## Further reading", "", "* [Calling services](calling-services.md)", "* [Services reference](index.md)",
          "* [About services](../../concepts/services.md)", "* [Permissions](../../concepts/permissions.md)", ""]
    txt = re.sub(r"\n{3,}", "\n\n", "\n".join(L))
    txt = txt.replace("G's part of the program", "the part LumaLink writes")
    txt = re.sub(r" \((?:models|effects|references|registry|sound|screens|stages|animation|materials|manifest|rules|"
                 r"validator|lumakit|services-[a-z.]+|[a-z]+\.md)( [0-9.]+[a-z]?)+\)", "", txt)
    txt = re.sub(r" \((?:services/[a-z.]+\.md|content/[a-z.]+\.md)\)", "", txt)
    return txt.replace("luma-format", "luma.json").replace("(see the contract)", "")


# ---- A page written by hand --------------------------------------------------------------------------------------

def struct_text(raw, struct):
    """A service's table as a page shows it: the header's typedef with its comments and blank lines left out."""
    m = re.search(r"typedef struct %s \{.*?\} %s;" % (struct, struct), raw, re.S)
    if not m:
        return None
    code = re.sub(r"/\*.*?\*/", "", m.group(0), flags=re.S)
    return [l.rstrip() for l in code.split("\n") if l.strip()]


def members(struct_lines):
    """{function: its declaration's lines as the header wraps them, without the table's indent} of a table."""
    out, cur = {}, []
    for line in struct_lines[1:-1]:
        cur.append(line[4:] if line.startswith("    ") else line.strip())
        if line.rstrip().endswith(";"):
            m = re.search(r"\(\*(\w+)\)", " ".join(cur))
            if m:
                out[m.group(1)] = cur
            cur = []
    return out


def service_rows(lines, start, services, problems):
    """The About part's table of the page's services (a column each, or one service's Name row): its Version,
    Permission and C import rows made from the headers. Each service needs its C import row."""
    i = start
    while i < len(lines) and not lines[i].startswith("|"):
        if lines[i].startswith("## ") and i > start:
            return
        i += 1
    if i >= len(lines):
        return
    head = [c.strip() for c in lines[i].strip("|").split("|")]
    rows = {}
    j = i + 2
    while j < len(lines) and lines[j].startswith("|"):
        cells = [c.strip() for c in lines[j].strip("|").split("|")]
        rows[cells[0]] = j
        j += 1
    named = [h.strip("`") for h in head[1:]]
    if "Name" in rows:  # one service, a row each
        cells = [c.strip() for c in lines[rows["Name"]].strip("|").split("|")]
        named = [cells[1].strip("`")]
        column = lambda r, v: "| %s | %s |" % (r, v)  # noqa: E731
    else:
        column = None
    for n in named:
        if n not in services:
            problems.append("the About table names %s, which no header declares" % n)
            return
    want = {"Version": [services[n][0] for n in named],
            "Permission": ["`%s`" % services[n][1] if services[n][1] else "None" for n in named],
            "C import": ['`LUMA_SERVICE(slot, "%s", "%s")`' % (n, services[n][0]) for n in named]}
    for row, values in want.items():
        if row not in rows:
            problems.append("the About table has no %s row" % row)
            continue
        if column:
            lines[rows[row]] = column(row, values[0])
        else:
            lines[rows[row]] = "| %s | %s |" % (row, " | ".join(values))


def hand_page(text, tables, structs, versions, forms, waits, problems):
    """The page's code blocks made the build's; its sections checked against the services' tables. A part is
    `## <service> functions`, `## <service>` or, on a page of one service, `## Functions`. In it, a `### <function>`
    section's C block is the table's member and its LumaScript block the compiler's form (and wait form). A C block of
    the whole table (`typedef struct Luma...Service`) is the header's and covers each of its functions; so does a
    LumaScript block of nothing but the service's forms. Returns (the page, the services whose functions it has)."""
    lines = text.split("\n")
    about = next((n for n, l in enumerate(lines) if l.startswith("## About")), None)
    if about is not None:
        service_rows(lines, about, versions, problems)
    own = re.findall(r'LUMA_SERVICE\(slot, "([a-z.]+)"', "\n".join(lines))
    out, service, seen, i = [], None, {}, 0

    def block_end(j):
        k = j + 1
        while lines[k] != "```":
            k += 1
        return k
    while i < len(lines):
        line = lines[i]
        m = re.match(r"^## `?([a-z][a-z0-9.]*)`?(?: functions)?$", line)
        if m and m.group(1) in tables:
            service = m.group(1)
            seen.setdefault(service, set())
        elif line == "## Functions" and own and own[0] in tables:
            service = own[0]
            seen.setdefault(service, set())
        elif line.startswith("## "):
            service = None
        if service and line == "```c" and lines[i + 1].startswith("typedef struct Luma"):
            k = block_end(i)
            name = re.match(r"typedef struct (\w+)", lines[i + 1]).group(1)
            made = structs.get(name)
            if made is None:
                problems.append("%s: a table %s that no header declares" % (service, name))
                made = lines[i + 1:k]
            else:
                whose = next((s for s, t in tables.items() if t["__struct__"] == name), service)
                seen.setdefault(whose, set()).update(f for f in tables[whose] if f != "__struct__")
            out += ["```c"] + made + ["```"]
            i = k + 1
            continue
        if service and line == "```lumascript":
            k = block_end(i)
            body = lines[i + 1:k]
            alias = service.split(".")[-1]
            if body and all(re.match(r"^(?:wait )?%s\.\w+\(" % re.escape(alias), l) for l in body):
                made = []
                for f in tables[service]:
                    if f != "__struct__" and forms.get((service, f)):
                        made.append(forms[(service, f)])
                        if (service, f) in waits:
                            made.append(waits[(service, f)])
                seen[service].update(f for f in tables[service] if f != "__struct__")
                out += ["```lumascript"] + made + ["```"]
                i = k + 1
                continue
        fm = re.match(r"^### `?(\w+)`?$", line)
        if service and fm:
            name = fm.group(1)
            member = tables[service].get(name) if name != "__struct__" else None
            out.append(line)
            i += 1
            if member is None:
                if re.match(r"^[a-z][a-z_0-9]*$", name):
                    problems.append("%s: a section %s, which its table does not have" % (service, name))
                continue  # (a section that is no function: ### Events, ### Spaces)
            seen[service].add(name)
            while i < len(lines) and not lines[i].startswith("## ") and not lines[i].startswith("### "):
                if lines[i] == "```c" and not lines[i + 1].startswith("typedef"):
                    k = block_end(i)
                    out += ["```c"] + member + ["```"]
                    i = k + 1
                    continue
                if lines[i] == "```lumascript" and re.match(r"^(?:wait )?\w+\.%s\(" % name, lines[i + 1]):
                    k = block_end(i)
                    form = forms.get((service, name))
                    if form is None:
                        problems.append("%s.%s: a LumaScript form, and the compiler gives it none" % (service, name))
                        out += lines[i:k + 1]
                    else:
                        out += ["```lumascript", form] + ([waits[(service, name)]] if (service, name) in waits else []) + ["```"]
                    i = k + 1
                    continue
                out.append(lines[i])
                i += 1
            continue
        out.append(line)
        i += 1
    for service, names in seen.items():
        missing = [f for f in tables[service] if f != "__struct__" and f not in names]
        if missing:
            problems.append("%s: no section for %s" % (service, ", ".join(missing)))
    return "\n".join(out), set(seen)


# ---- Every page ----------------------------------------------------------------------------------------------------

def made_pages(sdk, pages, forms, waits):
    """{page: (text it should have, problems)} for every service page."""
    headers = {h.name: parse(h) for h in sorted(sdk.glob("luma*.h"))}
    # Each service's table, {function: its member as declared, "__struct__": its typedef's name}, and each table's
    # typedef as a page shows it.
    tables, structs, versions = {}, {}, {}
    for h in sorted(sdk.glob("luma*.h")):
        raw = h.read_text(encoding="utf-8")
        for st in re.findall(r"typedef struct (Luma\w+Service) \{", raw):
            structs[st] = struct_text(raw, st)
    for r in headers.values():
        for s, name, ver, perm in services_of(r):
            tables[name] = {"__struct__": s["name"]}
            versions[name] = (ver, perm)
            lines = members(structs.get(s["name"]) or [])
            for code, _, _ in s["fns"]:
                if not code.startswith("LumaServiceTable"):
                    p = fn_parts(code)
                    if p:
                        tables[name][p[0]] = lines.get(p[0], [re.sub(r"\s+", " ", code).strip()])
    out, covered = {}, set()
    for page in sorted(pages.glob("*.md")):
        text = page.read_text(encoding="utf-8").replace("\r\n", "\n")
        problems = []
        h = re.search(r"^The header is `native/include/(luma\w*\.h)`", text, re.M)
        if h and "| Service | Version |" in text:
            if h.group(1) not in headers:
                problems.append("its header %s is not in the SDK" % h.group(1))
                out[page] = (text, problems)
                continue
            out[page] = (made_page(text, sdk / h.group(1), forms, waits), problems)
            covered |= {name for _, name, _, _ in services_of(headers[h.group(1)])}
        elif page.name not in ("calling-services.md", "index.md") and re.search(r"^## (?:`?[a-z][a-z0-9.]*`? functions|Functions)$", text, re.M):
            made, seen = hand_page(text, tables, structs, versions, forms, waits, problems)
            out[page] = (made, problems)
            covered |= seen
    # Every service has its functions on a page.
    for name in tables:
        if name not in covered and len(tables[name]) > 1:
            out.setdefault(pages / "index.md", (None, []))[1].append("no page has the functions of %s" % name)
    return out



def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--forms", required=True)
    p.add_argument("--sdk", default=str(ROOT / "native" / "include"))
    p.add_argument("--pages", default=str(ROOT / "docs" / "modding" / "reference" / "services"))
    p.add_argument("--check", action="store_true")
    args = p.parse_args(argv)
    forms, waits = read_forms(args.forms)
    bad = 0
    for page, (new, problems) in made_pages(pathlib.Path(args.sdk), pathlib.Path(args.pages), forms, waits).items():
        for problem in problems:
            print("%s: %s" % (page.name, problem))
            bad += 1
        if new is None:
            continue
        now = page.read_text(encoding="utf-8").replace("\r\n", "\n")
        if new != now:
            bad += 1
            if args.check:
                sys.stdout.writelines(difflib.unified_diff(now.splitlines(True), new.splitlines(True), "page/" + page.name,
                                                           "build/" + page.name, n=0))
                print("differs: %s" % page.name)
            else:
                page.write_text(new, encoding="utf-8", newline="\n")
                print("written: %s" % page.name)
    return 1 if args.check and bad else 0


if __name__ == "__main__":
    sys.exit(main())
