#!/usr/bin/env python3
"""Seed this tree's simulator card for a theme sweep.

    python3 tools_local/themesweep/seed.py <epub>...

Copies the EPUBs into fs_agent/books and writes tools_local/themesweep/.recent.json,
the recents list sweep.py installs for its "books" states. Each entry carries the
coverBmpPath Home needs to make a thumbnail (/.crosspoint/epub_<hash>/thumb_[HEIGHT].bmp),
where <hash> is std::hash<std::string> of the card path: computed by compiling a
three-line C++ program with the same host compiler the simulator uses, because
that hash is the library's and nothing else reproduces it.

The cover thumbnail is only generated for a book whose cache exists, which a
real device has for every recent book because it was opened. A seeded card has
none, so run sweep.py --prime once after seeding: it opens the first three.
"""
import json, os, re, shutil, subprocess, sys, tempfile, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = os.path.abspath(os.path.join(HERE, "..", ".."))
CARD = os.path.join(TREE, "fs_agent")


def card_hashes(paths):
    src = '#include <functional>\n#include <iostream>\n#include <string>\nint main(int c,char**v){for(int i=1;i<c;i++)std::cout<<std::hash<std::string>{}(v[i])<<"\\n";}\n'
    with tempfile.TemporaryDirectory() as d:
        cpp, exe = os.path.join(d, "h.cpp"), os.path.join(d, "h")
        open(cpp, "w").write(src)
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", cpp, "-o", exe], check=True)
        return subprocess.run([exe, *paths], capture_output=True, text=True, check=True).stdout.split()


def metadata(epub):
    z = zipfile.ZipFile(epub)
    opf = next(n for n in z.namelist() if n.endswith(".opf"))
    x = z.read(opf).decode("utf-8", "ignore")
    t = re.search(r"<dc:title[^>]*>(.*?)</dc:title>", x, re.S)
    a = re.search(r"<dc:creator[^>]*>(.*?)</dc:creator>", x, re.S)
    return (t.group(1).strip() if t else ""), (a.group(1).strip() if a else "")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    os.makedirs(os.path.join(CARD, "books"), exist_ok=True)
    os.makedirs(os.path.join(CARD, ".crosspoint"), exist_ok=True)
    books = []
    for src in sys.argv[1:]:
        dest = os.path.join(CARD, "books", os.path.basename(src))
        shutil.copy(src, dest)
        title, author = metadata(dest)
        books.append({"path": "/books/" + os.path.basename(src), "title": title, "author": author})
    for book, h in zip(books, card_hashes([b["path"] for b in books])):
        book["coverBmpPath"] = f"/.crosspoint/epub_{h}/thumb_[HEIGHT].bmp"
    json.dump({"books": books}, open(os.path.join(HERE, ".recent.json"), "w"), indent=1)
    print(f"seeded {len(books)} books; recents in {os.path.join(HERE, '.recent.json')}")


if __name__ == "__main__":
    main()
