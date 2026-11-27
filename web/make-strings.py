"""Package the browser entries from Piccolo's English and PO catalogs."""

import json
from pathlib import Path
import re


def english_catalog(path):
    catalog = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        key, _, value = line.partition("=")
        if key.strip().startswith("web_"):
            catalog[key.strip()] = json.loads('"' + value.strip() + '"')
    return catalog


def translated_catalog(path, english):
    catalog = {}
    for block in path.read_text(encoding="utf-8").split("\n\n"):
        fields = {}
        field = None
        for line in block.splitlines():
            name, _, value = line.partition(" ")
            if name in ("msgctxt", "msgid", "msgstr"):
                field = name
                fields[field] = json.loads(value)
            elif field and line.startswith('"'):
                fields[field] += json.loads(line)
        key = fields.get("msgctxt", "")
        if key in english and fields.get("msgid") == english[key] and fields.get("msgstr"):
            source_tokens = sorted(re.findall(r"\{[a-z_]+\}", english[key]))
            translated_tokens = sorted(re.findall(r"\{[a-z_]+\}", fields["msgstr"]))
            if source_tokens != translated_tokens:
                raise SystemExit(f"{path.name}: mismatched placeholders for {key}")
            catalog[key] = fields["msgstr"]
    return catalog


if __name__ == "__main__":
    root = Path(__file__).resolve().parent.parent
    english = english_catalog(root / "src" / "ui" / "strings.txt")
    catalogs = {"en": english}
    for translation in sorted((root / "lang").glob("*.po")):
        catalogs[translation.stem] = translated_catalog(translation, english)
    print("const PICCOLO_WEB_STRINGS = " + json.dumps(catalogs, separators=(",", ":")) + ";")
