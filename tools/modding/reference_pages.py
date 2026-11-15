"""The generated reference pages of docs/modding/reference (classes, events, tables, nerves): their frontmatter and
About sections around what each generator lists (gen_lumacore.py, events_list.py, tables_dump.py, nerves_list.py).

    write_page(kind, body, folder)    kind: classes, events or tables; body: the generator's text below the About section
    write_nerves(head, sections, folder)
                                      head: the text explaining nerves; sections: one "## <class>" section each, written
                                      to nerves.md (the index) and nerves-a-f.md, nerves-g-m.md, nerves-n-z.md
"""
import pathlib

PAGES = {
    "classes": ("Classes", "Every game class that derives from NameObj, with its parent and header.",
                "## About the game's classes",
                "This page lists every class of the game that derives from `NameObj`, with its parent class and its "
                "header. Use it to find a class for a `like` row or a hook."),
    "events": ("Events", "The game events an achievement can name, with what each carries.",
               "## About the game's events",
               "This page lists the events the game raises, with what each one carries. An achievement can wait for "
               "one, and a mod's code can hook the function that raises it."),
    "tables": ("Tables", "Every table of the game, its kinds and columns, for patches and merge files.",
               "## About the game's tables",
               "This page lists every table of the game, with its path, its kind and its columns. Use it to write "
               "table patches and merge files."),
    "nerves": ("Nerves", "Every nerve of the game's objects, with the code that runs in it.",
               "## About the game's nerves",
               "This page explains nerves and links to the lists of every nerve of the game, grouped by the first "
               "letter of the class."),
}
NERVE_GROUPS = (("a-f", "A to F", "F"), ("g-m", "G to M", "M"), ("n-z", "N to Z", "Z"))


def front(title, intro):
    return f"---\ntitle: {title}\nshortTitle: {title}\nintro: {intro}\ncontentType: reference\n---\n\n"


def _write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_page(kind, body, folder):
    title, intro, heading, about = PAGES[kind]
    _write(pathlib.Path(folder) / f"{kind}.md", front(title, intro) + f"{heading}\n\n{about}\n\n" + body.rstrip() + "\n")


def write_nerves(head, sections, folder):
    folder = pathlib.Path(folder)
    title, intro, heading, about = PAGES["nerves"]
    groups = {key: [] for key, _, _ in NERVE_GROUPS}
    for section in sections:
        first = section[3].upper()
        key = next(k for k, _, last in NERVE_GROUPS if first <= last)
        groups[key].append(section)
    index = front(title, intro) + f"{heading}\n\n{about}\n\n" + head.rstrip() + "\n\n## Nerves by class\n\n"
    for key, label, _ in NERVE_GROUPS:
        index += f"* [Classes {label}](nerves-{key}.md): {len(groups[key])} classes\n"
        page = front(f"Nerves, classes {label}", f"The nerves of the game's classes whose names start with {label}.")
        page += ("## About this list\n\n"
                 f"This page lists the nerves of the game's classes whose names start with {label}. Each section is one "
                 "class, with its source file. The Nerve column names the nerve, the Code column names the function that "
                 "runs every frame in it, and the On leaving column names the function that runs as an object leaves "
                 "it. For how to use nerves, see [Nerves](nerves.md).\n\n")
        _write(folder / f"nerves-{key}.md", page + "".join(groups[key]).rstrip() + "\n\n## Further reading\n\n* [Nerves](nerves.md)\n")
    _write(folder / "nerves.md", index + "\n## Further reading\n\n* [Hooks](lumascript/hooks.md)\n"
                                         "* [State machines](lumascript/state-machines.md)\n")
