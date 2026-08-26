#!/usr/bin/env python3
"""Generate complete VitaWave translation tables from the English catalog.

The generated files are reviewed artifacts used at build time; VitaWave never
contacts a translation service at runtime. printf placeholders are replaced by
opaque sentinels during translation and validated before any file is written.
"""

from __future__ import annotations

import json
import ast
import pathlib
import re
import time
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
I18N = ROOT / "src" / "i18n"
LANGUAGES = {"es": "es", "fr": "fr", "de": "de", "pt": "pt", "ru": "ru"}
ENTRY = re.compile(
    r'^VT_STR\(([^,]+),\s*("(?:\\.|[^"\\])*")\s*,\s*'
    r'("(?:\\.|[^"\\])*")\s*\)\s*$'
)
FORMAT = re.compile(r'%(?:[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|j|z|t|L)?[diuoxXfFeEgGaAcspn%])')


def catalog() -> list[tuple[str, str]]:
    result: list[tuple[str, str]] = []
    for path in sorted(I18N.glob("strings_*.def")):
        if path.name.startswith("strings_translation_"):
            continue
        for line in path.read_text(encoding="utf-8").splitlines():
            match = ENTRY.match(line.strip())
            if match:
                result.append((match.group(1).strip(), ast.literal_eval(match.group(3))))
    if not result:
        raise RuntimeError("No localizable strings found")
    return result


def protect(text: str) -> tuple[str, list[str]]:
    formats: list[str] = []

    def replace(match: re.Match[str]) -> str:
        formats.append(match.group(0))
        return f"ZXQFMT{len(formats) - 1}QXZ"

    return FORMAT.sub(replace, text), formats


def restore(text: str, formats: list[str]) -> str:
    for index, value in enumerate(formats):
        token = f"ZXQFMT{index}QXZ"
        if text.count(token) != 1:
            raise RuntimeError(f"placeholder {token} changed in {text!r}")
        text = text.replace(token, value)
    if "ZXQFMT" in text:
        raise RuntimeError(f"unexpected placeholder left in {text!r}")
    return text


def request(texts: list[str], language: str) -> list[str]:
    separators = [f"ZXQSEP{i:03d}QXZ" for i in range(len(texts) - 1)]
    combined = texts[0]
    for separator, text in zip(separators, texts[1:]):
        combined += f"\n{separator}\n{text}"
    query = urllib.parse.urlencode({
        "client": "gtx", "sl": "en", "tl": language, "dt": "t", "q": combined
    })
    url = "https://translate.googleapis.com/translate_a/single?" + query
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                payload = json.loads(response.read().decode("utf-8"))
            translated = "".join(part[0] for part in payload[0])
            pattern = "(?:\\n)?" + "(?:\\n)?".join(
                f"({re.escape(separator)})" for separator in []
            )
            pieces = [translated]
            for separator in separators:
                next_pieces: list[str] = []
                for piece in pieces:
                    if separator in piece:
                        left, right = piece.split(separator, 1)
                        next_pieces.extend((left, right))
                    else:
                        next_pieces.append(piece)
                pieces = next_pieces
            pieces = [piece.strip("\n ") for piece in pieces]
            if len(pieces) != len(texts):
                raise RuntimeError(f"batch split failed: {len(pieces)} != {len(texts)}")
            return pieces
        except Exception:
            if attempt == 3:
                raise
            time.sleep(1.5 * (attempt + 1))
    raise AssertionError("unreachable")


def translate(entries: list[tuple[str, str]], language: str) -> list[str]:
    protected: list[str] = []
    placeholders: list[list[str]] = []
    for _, text in entries:
        value, formats = protect(text)
        protected.append(value)
        placeholders.append(formats)
    output: list[str] = []
    cursor = 0
    while cursor < len(entries):
        end = cursor
        characters = 0
        while end < len(entries) and end - cursor < 20:
            addition = len(protected[end]) + 16
            if end > cursor and characters + addition > 2600:
                break
            characters += addition
            end += 1
        translated = request(protected[cursor:end], language)
        for offset, value in enumerate(translated):
            output.append(restore(value, placeholders[cursor + offset]))
        cursor = end
        print(f"{language}: {cursor}/{len(entries)}", flush=True)
        time.sleep(0.08)
    return output


def main() -> None:
    entries = catalog()
    for code, service_code in LANGUAGES.items():
        translations = translate(entries, service_code)
        lines = [
            "/* Generated from the English catalog by tools/generate-translations.py. */"
        ]
        for (identifier, _), value in zip(entries, translations):
            lines.append(f"VT_TRANSLATION({identifier}, {json.dumps(value, ensure_ascii=False)})")
        target = I18N / f"strings_translation_{code}.def"
        target.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
