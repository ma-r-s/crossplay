#!/usr/bin/env python3
"""Turn Project Gutenberg's RDF catalog into one JSON line per ebook.

    catalog.py --rdf rdf-files.tar.bz2 --out catalog.jsonl

Reads the daily tarball (https://www.gutenberg.org/cache/epub/feeds/
rdf-files.tar.bz2, about 127 MB, one RDF file per ebook) as a stream and
writes, per ebook: id, title, creators (name, birth, death), other agents
by role, languages, type, rights, subjects (LCSH), locc (LC classes),
bookshelves, issued, downloads (Gutenberg's own 30-day count), and the
byte size of each EPUB and text variant. No dependencies beyond the
standard library; the whole run is a few minutes.

This is the census input for rank.py. Nothing here decides anything.
"""

import argparse
import json
import sys
import tarfile
import xml.etree.ElementTree as ET

NS = {
    "rdf": "http://www.w3.org/1999/02/22-rdf-syntax-ns#",
    "pgterms": "http://www.gutenberg.org/2009/pgterms/",
    "dcterms": "http://purl.org/dc/terms/",
    "dcam": "http://purl.org/dc/dcam/",
    "marcrel": "http://id.loc.gov/vocabulary/relators/",
}
RDF_ABOUT = "{%s}about" % NS["rdf"]
RDF_RESOURCE = "{%s}resource" % NS["rdf"]

# The file variants worth a size. Keys are suffixes of the file URL.
VARIANTS = {
    ".epub.noimages": "epub_noimages",
    ".epub.images": "epub_images",
    ".epub3.images": "epub3_images",
    ".txt.utf-8": "txt",
    ".kf8.images": "kf8",
}


def text(el, path):
    found = el.find(path, NS)
    return found.text.strip() if found is not None and found.text else None


def agent(el):
    return {
        "name": text(el, "pgterms:agent/pgterms:name"),
        "birth": text(el, "pgterms:agent/pgterms:birthdate"),
        "death": text(el, "pgterms:agent/pgterms:deathdate"),
    }


def parse(xml_bytes):
    root = ET.fromstring(xml_bytes)
    ebook = root.find("pgterms:ebook", NS)
    if ebook is None:
        return None
    about = ebook.get(RDF_ABOUT, "")
    try:
        book_id = int(about.rsplit("/", 1)[-1])
    except ValueError:
        return None
    row = {
        "id": book_id,
        "title": text(ebook, "dcterms:title"),
        "alternative": [a.text.strip() for a in ebook.findall("dcterms:alternative", NS) if a.text],
        "creators": [agent(c) for c in ebook.findall("dcterms:creator", NS)],
        "agents": [],
        "languages": [],
        "type": text(ebook, "dcterms:type/rdf:Description/rdf:value"),
        "rights": text(ebook, "dcterms:rights"),
        "issued": text(ebook, "dcterms:issued"),
        "subjects": [],
        "locc": [],
        "bookshelves": [],
        "downloads": int(text(ebook, "pgterms:downloads") or 0),
        "sizes": {},
    }
    for child in ebook:
        if child.tag.startswith("{%s}" % NS["marcrel"]):
            role = child.tag.split("}", 1)[1]
            a = agent(child)
            if a["name"]:
                row["agents"].append({"role": role, "name": a["name"]})
    for lang in ebook.findall("dcterms:language/rdf:Description/rdf:value", NS):
        if lang.text:
            row["languages"].append(lang.text.strip())
    for subj in ebook.findall("dcterms:subject/rdf:Description", NS):
        member = subj.find("dcam:memberOf", NS)
        scheme = member.get(RDF_RESOURCE, "") if member is not None else ""
        value = text(subj, "rdf:value")
        if not value:
            continue
        if scheme.endswith("/LCC"):
            row["locc"].append(value)
        else:
            row["subjects"].append(value)
    for shelf in ebook.findall("pgterms:bookshelf/rdf:Description/rdf:value", NS):
        if shelf.text:
            row["bookshelves"].append(shelf.text.strip())
    for f in ebook.findall("dcterms:hasFormat/pgterms:file", NS):
        url = f.get(RDF_ABOUT, "")
        extent = text(f, "dcterms:extent")
        if not extent:
            continue
        for suffix, key in VARIANTS.items():
            if url.endswith(suffix):
                row["sizes"][key] = int(extent)
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rdf", required=True, help="rdf-files.tar.bz2")
    ap.add_argument("--out", required=True, help="catalog.jsonl to write")
    args = ap.parse_args()

    n_files = n_rows = n_bad = 0
    with tarfile.open(args.rdf, "r|bz2") as tar, open(args.out, "w") as out:
        for member in tar:
            if not member.name.endswith(".rdf"):
                continue
            n_files += 1
            data = tar.extractfile(member).read()
            try:
                row = parse(data)
            except ET.ParseError:
                row = None
            if row is None:
                n_bad += 1
                continue
            out.write(json.dumps(row, ensure_ascii=False) + "\n")
            n_rows += 1
            if n_rows % 10000 == 0:
                print(f"{n_rows} rows", file=sys.stderr, flush=True)
    print(f"files {n_files}, rows {n_rows}, unparsed {n_bad}", file=sys.stderr)


if __name__ == "__main__":
    main()
