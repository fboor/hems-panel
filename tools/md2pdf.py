#!/usr/bin/env python3
"""Render docs/install/anleitung.md -> docs/install/anleitung.pdf via
python-markdown + chromium. The system python3.10 has the markdown module;
chromium prints the HTML to PDF."""
import pathlib
import re
import subprocess
import sys

import markdown

ROOT = pathlib.Path(__file__).resolve().parent.parent
MD = ROOT / "docs" / "install" / "anleitung.md"
HTML = pathlib.Path("/tmp/hems-anleitung.html")
PDF = ROOT / "docs" / "install" / "anleitung.pdf"

with open(MD, "r", encoding="utf-8") as f:
    body = markdown.markdown(
        f.read(),
        extensions=["tables", "fenced_code", "sane_lists"],
        output_format="html5",
    )

css = """
:root { color-scheme: light; }
@page { size: A4; margin: 17mm 15mm 18mm 15mm; }
html { -webkit-print-color-adjust: exact; }
body {
  font-family: 'DejaVu Sans', 'Segoe UI', Arial, sans-serif;
  font-size: 10.2pt; line-height: 1.42; color: #1a1a1a; margin: 0;
}
h1 { font-size: 19pt; border-bottom: 2.5pt solid #1b5e20; padding-bottom: 4pt;
     margin: 0 0 10pt 0; color: #0b3d6b; }
h2 { font-size: 13.5pt; color: #0b3d6b; border-bottom: 1pt solid #b9d3ec;
     padding-bottom: 2pt; margin: 16pt 0 6pt 0; page-break-after: avoid; }
p { margin: 4pt 0; }
ul, ol { margin: 4pt 0 4pt 0; padding-left: 16pt; }
li { margin: 1.5pt 0; }
table { border-collapse: collapse; width: 100%; margin: 6pt 0 8pt 0;
  font-size: 9.3pt; }
th, td { border: 0.7pt solid #c5cdd5; padding: 3pt 6pt; text-align: left;
  vertical-align: top; }
th { background: #e8f0f8; color: #0b3d6b; font-weight: bold; }
tr:nth-child(even) td { background: #f7f9fb; }
strong { color: #0b3d6b; }
figure { margin: 8pt 0 10pt 0; page-break-inside: avoid; text-align: center; }
figure img { width: 88%%; }
figcaption { font-size: 9pt; color: #5a6672; margin-top: 3pt; }
"""

# Make relative image references absolute so chromium can load them.
# Relative to the markdown file's own folder (docs/install/), not docs/.
body = re.sub(
    r'src="(?!https?:|data:)([^"]+)"',
    lambda m: 'src="%s"' % (MD.parent / m.group(1)).resolve().as_uri(),
    body,
)

HTML.write_text(
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<style>%s</style></head><body>%s</body></html>" % (css, body),
    encoding="utf-8",
)

subprocess.run(
    ["chromium", "--headless", "--disable-gpu", "--no-sandbox",
     "--print-to-pdf=%s" % PDF, "--no-pdf-header-footer", HTML.as_uri()],
    check=True,
)
print("written: %s (%.1f kB)" % (PDF, PDF.stat().st_size / 1024))
