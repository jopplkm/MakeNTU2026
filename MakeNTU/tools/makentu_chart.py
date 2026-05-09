"""
Shared rhythm chart (.json): load/save and path resolution.

Format (UTF-8 JSON):
{
  "version": 1,
  "title": "...",
  "audio": "relative/or/absolute/path/to/track.mp3",
  "audio_offset_ms": 0,
  "notes": [ {"t_ms": 1500, "kind": "don"}, {"t_ms": 1620, "kind": "ka"}, {"t_ms": 2000, "kind": "both"} ]
}

t_ms: milliseconds from start of MP3 playback; kind: "don" | "ka" | "both"
  ("both" = don + ka within a short time window at that beat; see taiko_pygame.py)
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

FORMAT_VERSION = 1


class ChartParseError(Exception):
    pass


def normalize_chart_notes(notes_raw: Any) -> list[tuple[int, str]]:
    if notes_raw is None:
        notes_raw = []
    if not isinstance(notes_raw, list):
        raise ChartParseError("notes must be a list")
    out: list[tuple[int, str]] = []
    for i, n in enumerate(notes_raw):
        if not isinstance(n, dict):
            raise ChartParseError(f"notes[{i}] must be an object")
        try:
            tm = int(n["t_ms"])
        except (KeyError, TypeError, ValueError) as e:
            raise ChartParseError(f"notes[{i}].t_ms invalid") from e
        kind = str(n.get("kind", "don")).lower()
        if kind not in ("don", "ka", "both"):
            kind = "don"
        out.append((tm, kind))
    out.sort(key=lambda x: x[0])
    return out


def load_chart_json(chart_path: Path) -> tuple[list[tuple[int, str]], dict[str, Any]]:
    chart_path = chart_path.expanduser().resolve()
    try:
        text = chart_path.read_text(encoding="utf-8")
        obj = json.loads(text)
    except OSError as e:
        raise ChartParseError(str(e)) from e
    except json.JSONDecodeError as e:
        raise ChartParseError(f"Invalid JSON: {e}") from e
    if not isinstance(obj, dict):
        raise ChartParseError("root must be an object")

    notes = normalize_chart_notes(obj.get("notes"))
    audio_name = ""
    raw_audio = obj.get("audio")
    if isinstance(raw_audio, str):
        audio_name = raw_audio

    try:
        off = int(obj.get("audio_offset_ms", 0))
    except (TypeError, ValueError):
        off = 0

    title = obj.get("title")
    if not isinstance(title, str):
        title = ""

    meta = {
        "chart_path": chart_path,
        "audio": audio_name,
        "audio_offset_ms": off,
        "title": title,
        "version": int(obj.get("version", FORMAT_VERSION)),
    }
    return notes, meta


def resolve_audio_path(chart_path: Path, audio_field: str) -> Path | None:
    if not audio_field or not audio_field.strip():
        return None
    p = Path(audio_field)
    if p.is_absolute():
        return p if p.is_file() else None
    cand = (chart_path.parent / p).resolve()
    return cand if cand.is_file() else None


def save_chart_json(
    chart_path: Path,
    *,
    title: str,
    audio_ref: str,
    notes: list[tuple[int, str]],
    audio_offset_ms: int = 0,
) -> None:
    chart_path = chart_path.expanduser()
    chart_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "version": FORMAT_VERSION,
        "title": title,
        "audio": audio_ref,
        "audio_offset_ms": int(audio_offset_ms),
        "notes": [{"t_ms": int(t), "kind": k} for t, k in sorted(notes, key=lambda x: x[0])],
    }
    chart_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def song_length_from_notes(notes: list[tuple[int, str]], tail_ms: int = 4000) -> int:
    if not notes:
        return 8000
    return int(max(t for t, _ in notes) + tail_ms)
