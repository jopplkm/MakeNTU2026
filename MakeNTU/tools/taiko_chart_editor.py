#!/usr/bin/env python3
"""
Rhythm chart editor: load MP3, play/pause, scrub timeline, place don/ka notes, save JSON.

Uses same chart format as taiko_pygame.py (see makentu_chart.py).

  pip install -r requirements_taiko.txt
  python taiko_chart_editor.py
  python taiko_chart_editor.py --charts-dir D:\\Music\\makentu_charts

Keys:
  Ctrl+O  Open MP3   | Ctrl+E  Open chart (.json)
  Ctrl+S  Save (automatic path under charts folder unless already opened)
          | Ctrl+Shift+S  Save as… (file dialog / other folder)
  Space   Pause/play | Click/drag timeline to seek
  Z       Place DON (red) at playhead | X  Place KA (blue)
  Backspace Delete note nearest to playhead (±220 ms)
  [ / ]   Nudge calibration offset −10 ms / +10 ms (stored in chart)
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import pygame

try:
    from mutagen.mp3 import MP3
except ImportError:
    MP3 = None

from tkinter import Tk
from tkinter import filedialog

from makentu_chart import FORMAT_VERSION
from makentu_chart import load_chart_json
from makentu_chart import resolve_audio_path
from makentu_chart import save_chart_json


def mp3_duration_ms(path: Path) -> int:
    if MP3 is not None:
        try:
            return int(float(MP3(str(path)).info.length) * 1000.0)
        except Exception:
            pass
    snd = pygame.mixer.Sound(str(path))
    return max(1, int(snd.get_length() * 1000.0))


class Transport:
    """Wall-clock sync with pygame.mixer.music for editor preview."""

    def __init__(self) -> None:
        self.duration_ms = 1
        self._anchor_ms = 0
        self._play_wall = 0
        self.paused = True
        self.has_track = False

    def current_ms(self, now_tick: int) -> int:
        if self.paused:
            return min(max(0, self._anchor_ms), max(0, self.duration_ms - 1))
        elapsed = now_tick - self._play_wall
        return min(max(0, self._anchor_ms + elapsed), max(0, self.duration_ms - 1))

    def load_file(self, path: Path) -> None:
        pygame.mixer.music.load(str(path))
        self.duration_ms = max(1, mp3_duration_ms(path))
        self._anchor_ms = 0
        self._play_wall = pygame.time.get_ticks()
        self.paused = True
        self.has_track = True
        pygame.mixer.music.stop()

    def seek(self, ms: int, now_tick: int) -> None:
        ms = int(max(0, min(ms, self.duration_ms - 1)))
        self._anchor_ms = ms
        self._play_wall = now_tick
        if not self.paused:
            pygame.mixer.music.play(start=ms / 1000.0)
        else:
            pygame.mixer.music.stop()

    def toggle_pause(self, now_tick: int) -> None:
        if not self.has_track:
            return
        if self.paused:
            pygame.mixer.music.play(start=self._anchor_ms / 1000.0)
            self._play_wall = now_tick
            self.paused = False
        else:
            self._anchor_ms = self.current_ms(now_tick)
            pygame.mixer.music.pause()
            self.paused = True

    def tick_unpause_seek(self, now_tick: int) -> None:
        """If playing, sync anchor from wall clock (handles end of track)."""
        if self.paused or not self.has_track:
            return
        cur = self._anchor_ms + (now_tick - self._play_wall)
        if cur >= self.duration_ms - 20:
            self._anchor_ms = self.duration_ms - 1
            self.paused = True
            pygame.mixer.music.stop()


def pick_file_open(title: str, patterns: list[tuple[str, str]]) -> str | None:
    root = Tk()
    root.withdraw()
    root.attributes("-topmost", True)
    p = filedialog.askopenfilename(title=title, filetypes=patterns)
    root.destroy()
    return p or None


def pick_file_save(title: str, defaultextension: str) -> str | None:
    root = Tk()
    root.withdraw()
    root.attributes("-topmost", True)
    p = filedialog.asksaveasfilename(
        title=title,
        defaultextension=defaultextension,
        filetypes=[("Chart JSON", "*.json"), ("All", "*.*")],
    )
    root.destroy()
    return p or None


def rel_audio_for_save(chart_path: Path, audio_abs: Path) -> str:
    try:
        return str(Path(os.path.relpath(audio_abs.resolve(), chart_path.parent.resolve())))
    except ValueError:
        return str(audio_abs.resolve())


_WIN_BAD = '\\/:*?"<>|'


def _sanitize_stem(name: str, fallback: str) -> str:
    name = name.strip() or fallback
    out: list[str] = []
    for c in name:
        if ord(c) < 32 or c in _WIN_BAD:
            out.append("_")
        else:
            out.append(c)
    stem = "".join(out).strip(" .").rstrip("_") or fallback
    return stem[:200]


def _next_free_json(directory: Path, stem: str) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    candidate = directory / f"{stem}.json"
    if not candidate.exists():
        return candidate
    n = 2
    while True:
        p = directory / f"{stem}_{n}.json"
        if not p.exists():
            return p
        n += 1


def auto_chart_path(save_dir: Path, *, audio_path: Path | None, title: str) -> Path:
    """Pick a writable JSON path inside save_dir (no dialog)."""
    fallback = audio_path.stem if audio_path is not None else "chart"
    stem = _sanitize_stem(title.strip(), _sanitize_stem(fallback, "chart"))
    return _next_free_json(save_dir, stem)


def main() -> None:
    ap = argparse.ArgumentParser(description="Taiko rhythm chart editor")
    default_dir = (Path(__file__).resolve().parent / "charts").resolve()
    ap.add_argument(
        "--charts-dir",
        default=str(default_dir),
        help=f"Folder for Ctrl+S auto-save when no chart file is chosen yet (default: {default_dir})",
    )
    cli = ap.parse_args()
    charts_save_dir = Path(cli.charts_dir).expanduser().resolve()
    charts_save_dir.mkdir(parents=True, exist_ok=True)
    print(f"[chart editor] Ctrl+S auto-save folder: {charts_save_dir}", flush=True)

    os.environ.setdefault("SDL_VIDEO_CENTERED", "1")
    pygame.init()
    pygame.mixer.init(frequency=44100, size=-16, channels=2, buffer=2048)
    W, H = 1100, 640
    screen = pygame.display.set_mode((W, H))
    pygame.display.set_caption("MakeNTU Chart Editor")
    clock = pygame.time.Clock()
    font = pygame.font.SysFont("consolas", 22)
    small = pygame.font.SysFont("consolas", 18)

    transport = Transport()
    notes: list[tuple[int, str]] = []
    audio_path: Path | None = None
    chart_path: Path | None = None
    title = ""
    audio_offset_ms = 0
    scrubbing = False

    margin = 48
    tl_y = H - 120
    tl_h = 36

    def timeline_ms_from_x(x: int) -> int:
        inner = W - 2 * margin
        if inner <= 0:
            return 0
        frac = (x - margin) / inner
        return int(max(0, min(1, frac)) * (transport.duration_ms - 1))

    running = True
    while running:
        now = pygame.time.get_ticks()
        transport.tick_unpause_seek(now)
        play_ms = transport.current_ms(now)

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                mods = pygame.key.get_mods()
                ctrl = mods & pygame.KMOD_CTRL

                if ctrl and event.key == pygame.K_o:
                    path = pick_file_open("Open MP3", [("MP3", "*.mp3"), ("Audio", "*.mp3 *.ogg *.wav")])
                    if path:
                        p = Path(path)
                        audio_path = p
                        transport.load_file(p)
                        pygame.display.set_caption(f"Chart Editor — {p.name}")
                elif ctrl and event.key == pygame.K_e:
                    path = pick_file_open("Open chart JSON", [("JSON", "*.json")])
                    if path:
                        cp = Path(path)
                        try:
                            meta_notes, meta = load_chart_json(cp)
                            notes = list(meta_notes)
                            chart_path = cp
                            title = meta.get("title", "") or ""
                            audio_offset_ms = int(meta.get("audio_offset_ms", 0))
                            arel = meta.get("audio", "")
                            ap = resolve_audio_path(cp, arel) if arel else None
                            if ap is not None and ap.is_file():
                                audio_path = ap
                                transport.load_file(ap)
                                pygame.display.set_caption(f"Chart Editor — {ap.name}")
                            else:
                                audio_path = None
                                transport.paused = True
                                pygame.mixer.music.stop()
                                transport.has_track = notes != []
                                last_t = max((t for t, _ in notes), default=0)
                                transport.duration_ms = max(1, last_t + 6000)
                        except Exception as e:
                            print(f"Open chart failed: {e}", file=sys.stderr)
                elif ctrl and event.key == pygame.K_s:
                    if mods & pygame.KMOD_SHIFT:
                        path = pick_file_save("Save chart", ".json")
                        if path:
                            chart_path = Path(path)
                    elif chart_path is None:
                        chart_path = auto_chart_path(
                            charts_save_dir,
                            audio_path=audio_path,
                            title=title,
                        )
                    if chart_path is not None:
                        ref = ""
                        if audio_path is not None:
                            ref = rel_audio_for_save(chart_path, audio_path)
                        save_chart_json(
                            chart_path,
                            title=title,
                            audio_ref=ref,
                            notes=notes,
                            audio_offset_ms=audio_offset_ms,
                        )
                elif event.key == pygame.K_SPACE:
                    transport.toggle_pause(now)
                elif event.key == pygame.K_z:
                    notes.append((play_ms, "don"))
                    notes.sort(key=lambda x: x[0])
                elif event.key == pygame.K_x and not ctrl:
                    notes.append((play_ms, "ka"))
                    notes.sort(key=lambda x: x[0])
                elif event.key == pygame.K_BACKSPACE:
                    if not notes:
                        pass
                    else:
                        best_i = -1
                        best_d = 10**9
                        for i, (t, _) in enumerate(notes):
                            d = abs(t - play_ms)
                            if d < best_d:
                                best_d = d
                                best_i = i
                        if best_i >= 0 and best_d <= 220:
                            del notes[best_i]
                elif event.key == pygame.K_LEFT:
                    transport.seek(max(0, play_ms - 1000), now)
                elif event.key == pygame.K_RIGHT:
                    transport.seek(min(transport.duration_ms - 1, play_ms + 1000), now)
                elif event.key == pygame.K_LEFTBRACKET:
                    audio_offset_ms -= 10
                elif event.key == pygame.K_RIGHTBRACKET:
                    audio_offset_ms += 10

            elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                mx, my = event.pos
                if tl_y <= my <= tl_y + tl_h + 20 and margin <= mx <= W - margin:
                    scrubbing = True
                    transport.seek(timeline_ms_from_x(mx), now)
                    if not transport.paused:
                        pygame.mixer.music.play(start=transport._anchor_ms / 1000.0)
                        transport._play_wall = now
            elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
                scrubbing = False
            elif event.type == pygame.MOUSEMOTION:
                if scrubbing:
                    mx = event.pos[0]
                    transport.seek(timeline_ms_from_x(mx), pygame.time.get_ticks())
                    if not transport.paused:
                        pygame.mixer.music.play(start=transport._anchor_ms / 1000.0)
                        transport._play_wall = pygame.time.get_ticks()

        screen.fill((28, 26, 34))
        y = 12
        for line in [
            "Ctrl+S saves into folder (shown below); Ctrl+Shift+S saves elsewhere via dialog",
            "Ctrl+O Open MP3   Ctrl+E Open chart",
            "Space Play/Pause — timeline: click/drag to seek",
            "Z = Don (red)   X = Ka (blue)   Backspace = delete nearest   ←/→ seek ±1s",
            "[ ] = calibration offset ms (saved in JSON, used in game)",
        ]:
            screen.blit(font.render(line, True, (220, 215, 235)), (16, y))
            y += 30

        ap_str = str(audio_path) if audio_path else "(no audio — Ctrl+O)"
        ch_str = str(chart_path) if chart_path else "(unsaved chart)"
        screen.blit(small.render(f"Audio: {ap_str}", True, (160, 200, 180)), (16, y + 8))
        screen.blit(small.render(f"Chart: {ch_str}", True, (180, 180, 200)), (16, y + 30))
        sdir = small.render(f"Ctrl+S folder: {charts_save_dir}", True, (170, 200, 190))
        screen.blit(sdir, (16, y + 50))
        screen.blit(
            small.render(
                f"Offset {audio_offset_ms} ms  |  Notes {len(notes)}  |  ver {FORMAT_VERSION}",
                True,
                (200, 200, 210),
            ),
            (16, y + 72),
        )

        def fmt(ms: int) -> str:
            s = ms // 1000
            m = s // 60
            s %= 60
            return f"{m:d}:{s:02d}.{ms % 1000:03d}"

        hud = f"{fmt(play_ms)} / {fmt(transport.duration_ms)}  {'PAUSED' if transport.paused else 'PLAY'}"
        screen.blit(font.render(hud, True, (255, 230, 160)), (16, H - 168))

        pygame.draw.rect(screen, (55, 50, 65), (margin, tl_y, W - 2 * margin, tl_h), border_radius=6)
        if transport.duration_ms > 1:
            frac = play_ms / (transport.duration_ms - 1)
            pw = int((W - 2 * margin) * frac)
            pygame.draw.rect(screen, (120, 200, 255), (margin, tl_y, pw, tl_h), border_radius=6)
        px = margin + int((W - 2 * margin) * (play_ms / max(1, transport.duration_ms - 1)))
        pygame.draw.line(screen, (255, 255, 120), (px, tl_y - 6), (px, tl_y + tl_h + 6), 3)

        for t, k in notes:
            if transport.duration_ms <= 1:
                continue
            nx = margin + int((W - 2 * margin) * (t / max(1, transport.duration_ms - 1)))
            col = (220, 70, 70) if k == "don" else (70, 130, 240)
            pygame.draw.line(screen, col, (nx, tl_y - 14), (nx, tl_y + tl_h + 14), 2)

        pygame.display.flip()
        clock.tick(60)

    pygame.mixer.music.stop()
    pygame.quit()


if __name__ == "__main__":
    main()
