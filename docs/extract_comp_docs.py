#!/usr/bin/env python3
"""Generate one markdown page per McStas component from McDoc headers.

Each ``*.comp`` file is expected to start with a McDoc comment block using the
standard McStas sections (``%I``/``%IDENTIFICATION``, ``%D``/``%DESCRIPTION``,
``%P``/``%PARAMETERS``, ``%L``/``%LINKS``, ``%E``/``%END``) followed by the
component grammar (``DEFINE COMPONENT`` / ``SETTING PARAMETERS``).  Parameter
types and defaults are read from the grammar; parameter descriptions from the
``%P`` section (``name: [unit] description`` lines).

Usage: extract_comp_docs.py <components-dir> <output-dir>
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

SECTION_ALIASES = {
    "I": "identification",
    "IDENTIFICATION": "identification",
    "D": "description",
    "DESCRIPTION": "description",
    "P": "parameters",
    "PARAMETERS": "parameters",
    "L": "links",
    "LINKS": "links",
    "E": "end",
    "END": "end",
}

PARAM_LINE = re.compile(r"^(\w+):\s*(\[[^]]*])?\s*(.*)$")


def strip_comment_line(line: str) -> str:
    """Remove leading comment decoration from one line of the header block."""
    line = line.rstrip()
    line = re.sub(r"^\s*/?\*+/?", "", line)
    return line.removeprefix(" ")


def mcdoc_sections(text: str) -> dict[str, list[str]]:
    """Split the first comment block into McDoc sections keyed by full name."""
    match = re.search(r"/\*.*?\*/", text, flags=re.DOTALL)
    if not match or text[: match.start()].strip():
        return {}  # no comment block before the component definition
    sections: dict[str, list[str]] = {}
    current = None
    for raw in match.group(0).splitlines():
        line = strip_comment_line(raw)
        tag = re.fullmatch(r"%(\w+)", line.strip())
        key = SECTION_ALIASES.get(tag.group(1).upper()) if tag else None
        if key == "end":
            break
        if key:
            current = key
            sections[current] = []
        elif current is not None:
            sections[current].append(line)
    return sections


def parse_parameter_docs(lines: list[str]) -> dict[str, tuple[str, str]]:
    """Map parameter name -> (unit, description) from %P section lines."""
    docs: dict[str, tuple[str, str]] = {}
    name = None
    for line in lines:
        m = PARAM_LINE.match(line.strip())
        if m:
            name = m.group(1)
            unit = (m.group(2) or "").strip("[]").strip()
            docs[name] = (unit, m.group(3).strip())
        elif name and line.strip():  # continuation of the previous entry
            unit, desc = docs[name]
            docs[name] = (unit, f"{desc} {line.strip()}".strip())
    return docs


def parse_setting_parameters(text: str) -> list[tuple[str, str, str]]:
    """Return (name, type, default) for each SETTING PARAMETERS entry."""
    m = re.search(r"SETTING\s+PARAMETERS\s*\(", text)
    if not m:
        return []
    depth, i = 1, m.end()
    while i < len(text) and depth:
        depth += {"(": 1, ")": -1}.get(text[i], 0)
        i += 1
    body = text[m.end() : i - 1]
    body = re.sub(r"//[^\n]*", "", body)  # inline comments
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
    params = []
    for entry in body.split(","):
        entry = entry.strip()
        if not entry:
            continue
        decl, _, default = entry.partition("=")
        words = decl.split()
        name = words[-1]
        ptype = " ".join(words[:-1]) or "double"
        params.append((name, ptype, default.strip()))
    return params


def fence_examples(lines: list[str]) -> list[str]:
    """Wrap paragraphs starting with ``Example:`` in fenced code blocks."""
    out: list[str] = []
    fenced = False
    for line in lines:
        if not fenced and line.lstrip().startswith("Example:"):
            out.append("```")
            fenced = True
        elif fenced and not line.strip():
            out.append("```")
            fenced = False
        out.append(line)
    if fenced:
        out.append("```")
    return out


def short_description(sections: dict[str, list[str]]) -> str:
    """First %I line that is not Written by/Date/Origin metadata."""
    for line in sections.get("identification", []):
        text = line.strip()
        if text and not re.match(r"(Written by|Date|Origin|Modified by|Version):", text):
            return text
    return ""


def component_page(path: Path) -> tuple[str, str] | None:
    """Return (markdown page, one-line index hook) for one component file."""
    text = path.read_text(encoding="utf-8")
    name_match = re.search(r"DEFINE\s+COMPONENT\s+(\w+)", text)
    if not name_match:
        return None
    name = name_match.group(1)
    sections = mcdoc_sections(text)
    param_docs = parse_parameter_docs(sections.get("parameters", []))
    params = parse_setting_parameters(text)

    out = [f"# {name} component {{#comp_{name}}}", ""]
    ident = [l for l in sections.get("identification", []) if l.strip()]
    if ident:
        out += ident + [""]
    if sections.get("description"):
        out += ["## Description", ""]
        out += fence_examples(sections["description"]) + [""]
    if params:
        out += ["## Parameters", ""]
        out += ["| Name | Type | Default | Unit | Description |",
                "|------|------|---------|------|-------------|"]
        for pname, ptype, default in params:
            unit, desc = param_docs.get(pname, ("", ""))
            default_txt = f"`{default}`" if default else "*required*"
            out.append(f"| `{pname}` | `{ptype}` | {default_txt} | {unit} | {desc} |")
        out.append("")
        undocumented = [p for p, _, _ in params if p not in param_docs]
        if undocumented:
            print(f"warning: {path.name}: parameters missing from %P section: "
                  f"{', '.join(undocumented)}", file=sys.stderr)
    links = [l for l in sections.get("links", []) if l.strip()]
    if links:
        out += ["## Links", ""] + links + [""]
    out += [f"Source: `readout_core/components/{path.name}`", ""]
    return "\n".join(out), short_description(sections)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    comp_dir, out_dir = Path(sys.argv[1]), Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)
    index = ["# McStas components {#components}", "",
             "Components provided by this package, generated from their",
             "in-source McDoc headers (also viewable with `mcdoc`).", ""]
    for path in sorted(comp_dir.glob("*.comp")):
        result = component_page(path)
        if result is None:
            print(f"warning: {path.name}: no DEFINE COMPONENT found, skipped",
                  file=sys.stderr)
            continue
        page, hook = result
        md_name = f"{path.stem}.md"
        (out_dir / md_name).write_text(page, encoding="utf-8")
        index.append(f"- [{path.stem}]({md_name}) — {hook}")
    index.append("")
    (out_dir / "components.md").write_text("\n".join(index), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
