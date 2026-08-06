#!/usr/bin/env python3
"""Dependency-free locale selection and JSON translation loading."""

from __future__ import annotations

import json
import locale
import os
from pathlib import Path
from typing import Mapping


SUPPORTED_LANGUAGES = (
    "en", "he", "ar", "de", "es", "fr", "it", "pt", "nl", "ru", "uk",
    "pl", "tr", "zh", "zh_Hant", "ja", "ko", "sv", "da", "nb", "fi",
    "cs", "sk", "hu", "ro", "el", "id", "vi", "fa", "hi", "th", "ca",
    "bg", "hr", "sr_Latn", "sl", "lt", "lv", "et", "is", "ms", "fil",
    "bn", "ta", "te", "mr", "ur",
)
RTL_LANGUAGES = frozenset(("he", "ar", "fa", "ur"))
TRANSLATIONS_DIR = Path(__file__).resolve().parent / "translations"


def system_language(environment: Mapping[str, str] | None = None) -> str:
    """Select a supported language from an override or the process locale."""
    values = environment if environment is not None else os.environ
    requested = values.get("WINE4OFFICE_LANGUAGE", "").strip()
    if not requested:
        requested = next(
            (values.get(name, "").strip() for name in ("LC_ALL", "LC_MESSAGES", "LANG")
             if values.get(name, "").strip()),
            "",
        )
    if not requested:
        requested = locale.getlocale()[0] or "en"
    normalized = requested.replace("-", "_").split(".", 1)[0]
    parts = normalized.split("_")
    language = parts[0].casefold()
    if language == "zh" and any(
        part.casefold() in {"tw", "hk", "mo", "hant"} for part in parts[1:]
    ):
        language = "zh_Hant"
    if language == "sr":
        language = "sr_Latn"
    aliases = {
        "iw": "he", "heb": "he", "ara": "ar", "eng": "en", "sh": "sr_Latn",
        "c": "en", "posix": "en",
    }
    language = aliases.get(language, language)
    return language if language in SUPPORTED_LANGUAGES else "en"


def _load_catalog(language: str) -> dict[str, str]:
    path = TRANSLATIONS_DIR / f"{language}.json"
    with path.open(encoding="utf-8") as stream:
        catalog = json.load(stream)
    if not isinstance(catalog, dict) or not all(
        isinstance(source, str) and source
        and isinstance(translated, str) and translated
        for source, translated in catalog.items()
    ):
        raise ValueError(f"Invalid translation catalog: {path}")
    return catalog


CATALOGS = {
    language: _load_catalog(language)
    for language in SUPPORTED_LANGUAGES
}


def translate(text: str, language: str) -> str:
    return CATALOGS.get(language, {}).get(text, text)
