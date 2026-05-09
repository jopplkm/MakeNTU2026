#!/usr/bin/env python3
"""
Taiko-style rhythm game: STM32 WiFi/TCP sends lines like
  hit:1 avg:1234 L:250 H:400
Each hit:1 is treated as a drum strike (same as Space for testing).

Run Python first, then power/connect the board (same port as APP_WIFI_REMOTE_PORT).

  pip install -r requirements_taiko.txt
  python taiko_pygame.py
  python taiko_pygame.py --port 8002 --no-tcp   # keyboard only
  python taiko_pygame.py --chart mysong.json    # custom chart + MP3 path inside JSON
"""

from __future__ import annotations

import argparse
import os
import queue
import re
import socket
import sys
import threading
from dataclasses import dataclass, field
from pathlib import Path

import pygame

from makentu_chart import ChartParseError
from makentu_chart import load_chart_json
from makentu_chart import resolve_audio_path
from makentu_chart import song_length_from_notes

# --- Match STM32 format: hit:1 avg:... (main.c snprintf) ---
HIT_LINE_RE = re.compile(r"hit\s*:\s*1\b", re.IGNORECASE)

# Beat chart: (time_ms from song start, kind: "don" | "ka")
# EMG / Space counts as "don". Key K = ka (optional second drum on keyboard).
DEMO_CHART: list[tuple[int, str]] = [
    (1800, "don"),
    (2600, "ka"),
    (3400, "don"),
    (4200, "don"),
    (5000, "ka"),
    (5800, "don"),
    (6600, "don"),
    (7400, "ka"),
    (8200, "don"),
    (9000, "don"),
    (9800, "ka"),
    (10600, "don"),
    (11400, "don"),
    (12200, "ka"),
    (13000, "don"),
]

SONG_LENGTH_MS_DEFAULT = 14500

# Judgement (ms)
WINDOW_PERFECT = 70
WINDOW_GOOD = 130
WINDOW_MISS_PAST = 160

# Scroll: judgement line x; notes move right -> left
JUDGE_X = 220
PIXELS_PER_MS = 0.42
NOTE_RADIUS = 28


@dataclass
class Note:
    t_hit: int
    kind: str
    hit_result: str | None = None  # "perfect", "good", or None / "miss"


@dataclass
class GameState:
    notes: list[Note] = field(default_factory=list)
    t0: int = 0
    score: int = 0
    combo: int = 0
    max_combo: int = 0
    counts: dict[str, int] = field(
        default_factory=lambda: {"perfect": 0, "good": 0, "miss": 0}
    )
    finished: bool = False


def tcp_listener(
    host: str,
    port: int,
    hit_queue: queue.Queue[str],
    stop_event: threading.Event,
) -> None:
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        srv.bind((host, port))
    except OSError as e:
        print(f"[taiko] TCP bind failed {host}:{port}: {e}", flush=True)
        return
    srv.listen(1)
    srv.settimeout(0.5)
    buf = b""
    conn: socket.socket | None = None
    while not stop_event.is_set():
        if conn is None:
            try:
                conn, _ = srv.accept()
                conn.settimeout(0.05)
            except TimeoutError:
                continue
            except OSError:
                break
            buf = b""
        assert conn is not None
        try:
            chunk = conn.recv(4096)
            if not chunk:
                conn.close()
                conn = None
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", errors="replace").strip()
                if text and HIT_LINE_RE.search(text):
                    hit_queue.put("don")
        except TimeoutError:
            continue
        except OSError:
            try:
                conn.close()
            except OSError:
                pass
            conn = None
    try:
        srv.close()
    except OSError:
        pass


def try_judge(
    state: GameState,
    now_ms: int,
    strike_kind: str,
) -> str | None:
    """Register a hit; return judgement label or None if nothing to hit."""
    best: Note | None = None
    best_delta = 10**9
    for n in state.notes:
        if n.hit_result is not None:
            continue
        if strike_kind == "don" and n.kind == "ka":
            continue
        if strike_kind == "ka" and n.kind == "don":
            continue
        d = abs(n.t_hit - now_ms)
        if d < best_delta:
            best_delta = d
            best = n
    if best is None:
        return None
    if best_delta <= WINDOW_PERFECT:
        label = "perfect"
    elif best_delta <= WINDOW_GOOD:
        label = "good"
    else:
        return None
    best.hit_result = label
    state.counts[label] += 1
    pts = 300 if label == "perfect" else 150
    state.score += pts + state.combo * 2
    state.combo += 1
    state.max_combo = max(state.max_combo, state.combo)
    return label


def update_misses(state: GameState, now_ms: int) -> None:
    for n in state.notes:
        if n.hit_result is not None:
            continue
        if now_ms > n.t_hit + WINDOW_MISS_PAST:
            n.hit_result = "miss"
            state.counts["miss"] += 1
            state.combo = 0


def draw_game(
    screen: pygame.Surface,
    font: pygame.font.Font,
    big_font: pygame.font.Font,
    state: GameState,
    now_ms: int,
    hit_flash_ms: int,
    last_judge: str | None,
) -> None:
    w, h = screen.get_size()
    screen.fill((24, 20, 28))
    # Track
    pygame.draw.rect(screen, (50, 42, 55), (0, h // 2 - 50, w, 100))
    pygame.draw.line(screen, (200, 180, 100), (JUDGE_X, h // 2 - 60), (JUDGE_X, h // 2 + 60), 4)
    pygame.draw.circle(screen, (255, 220, 120), (JUDGE_X, h // 2), 14)

    for n in state.notes:
        x = int(JUDGE_X + (n.t_hit - now_ms) * PIXELS_PER_MS)
        cy = h // 2
        if n.kind == "don":
            color = (220, 60, 60)
            edge = (255, 200, 200)
        else:
            color = (60, 120, 220)
            edge = (200, 220, 255)
        if n.hit_result == "miss":
            color = tuple(c // 3 for c in color)
        pygame.draw.circle(screen, color, (x, cy), NOTE_RADIUS)
        pygame.draw.circle(screen, edge, (x, cy), NOTE_RADIUS, 3)

    hud_y = 16
    screen.blit(big_font.render(f"Score {state.score}", True, (240, 240, 245)), (20, hud_y))
    screen.blit(
        font.render(f"Combo {state.combo}  (best {state.max_combo})", True, (200, 200, 210)),
        (20, hud_y + 52),
    )
    screen.blit(
        font.render(
            f"P {state.counts['perfect']}  G {state.counts['good']}  Miss {state.counts['miss']}",
            True,
            (180, 175, 190),
        ),
        (20, hud_y + 82),
    )
    if hit_flash_ms > 0:
        alpha = min(255, hit_flash_ms * 4)
        s = pygame.Surface((w, 80), pygame.SRCALPHA)
        s.fill((255, 255, 200, alpha // 3))
        screen.blit(s, (0, h // 2 - 40))
    if last_judge:
        col = {"perfect": (255, 240, 100), "good": (140, 220, 255), "miss": (200, 80, 80)}.get(
            last_judge, (255, 255, 255)
        )
        t = big_font.render(last_judge.upper(), True, col)
        screen.blit(t, (w // 2 - t.get_width() // 2, h // 2 - 120))

    if state.finished:
        ov = pygame.Surface((w, h), pygame.SRCALPHA)
        ov.fill((0, 0, 0, 170))
        screen.blit(ov, (0, 0))
        msg = big_font.render("FINISH — R to replay", True, (255, 255, 255))
        screen.blit(msg, (w // 2 - msg.get_width() // 2, h // 2 - 30))


def main() -> None:
    parser = argparse.ArgumentParser(description="Taiko-like game with STM32 TCP hits")
    parser.add_argument("--host", default="0.0.0.0", help="TCP bind address")
    parser.add_argument("--port", type=int, default=8002, help="Match APP_WIFI_REMOTE_PORT")
    parser.add_argument("--no-tcp", action="store_true", help="Disable WiFi listener")
    parser.add_argument(
        "--chart",
        default=None,
        help="Rhythm chart JSON (from taiko_chart_editor.py); contains note times and MP3 path",
    )
    parser.add_argument(
        "--audio",
        default=None,
        help="Override MP3 path (otherwise use path inside chart JSON)",
    )
    parser.add_argument(
        "--skip-help",
        action="store_true",
        help="Skip title screen; start chart immediately (use if keys seem ignored)",
    )
    args = parser.parse_args()

    chart_notes: list[tuple[int, str]] = list(DEMO_CHART)
    song_length_ms = SONG_LENGTH_MS_DEFAULT
    audio_path: Path | None = None
    audio_offset_ms = 0
    chart_label = "demo chart"
    if args.chart:
        cp = Path(args.chart).expanduser().resolve()
        try:
            chart_notes, meta = load_chart_json(cp)
            audio_offset_ms = int(meta.get("audio_offset_ms", 0))
            t = meta.get("title")
            chart_label = str(t) if isinstance(t, str) and t.strip() else cp.name
            arel = meta.get("audio", "")
            resolved = resolve_audio_path(cp, arel) if isinstance(arel, str) and arel.strip() else None
            if args.audio:
                ap = Path(args.audio).expanduser().resolve()
                audio_path = ap if ap.is_file() else None
            else:
                audio_path = resolved if resolved and resolved.is_file() else None
            song_length_ms = song_length_from_notes(chart_notes)
            if audio_path is None and args.audio is None and isinstance(arel, str) and arel.strip():
                print(
                    f"[taiko] Chart audio not found (looked next to JSON): {arel!r}\n"
                    "        Use --audio full\\path\\to\\file.mp3",
                    file=sys.stderr,
                )
        except ChartParseError as e:
            print(f"[taiko] Bad chart file: {e}", file=sys.stderr)
            sys.exit(1)

    os.environ.setdefault("SDL_VIDEO_CENTERED", "1")
    pygame.init()
    pygame.mixer.init(frequency=44100, size=-16, channels=2, buffer=2048)
    if pygame.display.get_driver() == "dummy":
        print(
            "[taiko] Pygame is using the dummy video driver (no real window). "
            "Run on a PC with a display or unset SDL_VIDEODRIVER.",
            flush=True,
        )
    pygame.display.set_caption("MakeNTU Taiko — hit:1 / Space don / X ka")
    screen = pygame.display.set_mode((960, 540))
    clock = pygame.time.Clock()
    font = pygame.font.SysFont("consolas", 26)
    big_font = pygame.font.SysFont("consolas", 40, bold=True)

    hit_queue: queue.Queue[str] = queue.Queue()
    stop_tcp = threading.Event()
    tcp_thread: threading.Thread | None = None
    if not args.no_tcp:
        tcp_thread = threading.Thread(
            target=tcp_listener,
            args=(args.host, args.port, hit_queue, stop_tcp),
            daemon=True,
        )
        tcp_thread.start()

    def reset_game() -> GameState:
        pygame.mixer.music.stop()
        g = GameState()
        g.t0 = pygame.time.get_ticks()
        g.notes = [Note(t_hit=t, kind=k) for t, k in chart_notes]
        if audio_path is not None and audio_path.is_file():
            pygame.mixer.music.load(str(audio_path))
            pygame.mixer.music.play(start=0.0)
        return g

    state = reset_game()

    hit_flash_ms = 0
    last_judge: str | None = None
    judge_until_ms = 0
    showing_help = not args.skip_help
    help_start_ticks = pygame.time.get_ticks()

    running = True
    while running:
        now_wall = pygame.time.get_ticks()

        if showing_help:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        running = False
                    else:
                        showing_help = False
                        state = reset_game()
                elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    showing_help = False
                    state = reset_game()
            # Proof the loop is alive; auto-start after 5s so the game never looks "stuck"
            if showing_help and (now_wall - help_start_ticks) > 5000:
                showing_help = False
                state = reset_game()
            pygame.event.pump()
            screen.fill((20, 18, 30))
            lines = [
                f"Chart: {chart_label}",
                "STM32: hit:1 line → DON (red)  |  Space = Don   X = Ka",
                "藍色(ka)需按鍵 X；請先對 pygame 視窗按一下再按鍵",
                "",
                "Click LEFT mouse here, press any key, or wait 5s to auto-start",
                "",
                f"TCP {args.host}:{args.port}  ('--no-tcp' if no board)",
            ]
            y_line = 70
            for line in lines:
                if not line.strip():
                    y_line += 20
                    continue
                hue = (220, 215, 230) if (now_wall // 400) % 2 == 0 else (255, 200, 120)
                surf = font.render(line, True, hue)
                screen.blit(surf, (40, y_line))
                y_line += 32
            hint = font.render("(ESC to quit)", True, (140, 140, 150))
            screen.blit(hint, (40, screen.get_height() - 44))
            pygame.display.flip()
            clock.tick(60)
            continue

        elapsed = now_wall - state.t0
        chart_ms = elapsed + audio_offset_ms

        while True:
            try:
                strike = hit_queue.get_nowait()
            except queue.Empty:
                break
            j = try_judge(state, chart_ms, strike)
            if j:
                hit_flash_ms = 18
                last_judge = j
                judge_until_ms = now_wall + 380
            else:
                hit_flash_ms = 6
                # No playable note in window (do not confuse with timing miss).
                judge_until_ms = now_wall + 220

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if state.finished:
                    if event.key == pygame.K_r:
                        state = reset_game()
                        last_judge = None
                    continue
                if event.key == pygame.K_SPACE:
                    j = try_judge(state, chart_ms, "don")
                    if j:
                        last_judge = j
                        judge_until_ms = now_wall + 380
                        hit_flash_ms = 18
                    else:
                        hit_flash_ms = 8
                        judge_until_ms = now_wall + 180
                elif event.key == pygame.K_x:
                    j = try_judge(state, chart_ms, "ka")
                    if j:
                        last_judge = j
                        judge_until_ms = now_wall + 380
                        hit_flash_ms = 18
                    else:
                        hit_flash_ms = 8
                        judge_until_ms = now_wall + 180

        update_misses(state, chart_ms)

        if now_wall > judge_until_ms:
            last_judge = None

        if hit_flash_ms > 0:
            hit_flash_ms -= 1

        draw_game(screen, font, big_font, state, chart_ms, hit_flash_ms, last_judge)

        if not state.finished and chart_ms > song_length_ms + 800:
            state.finished = True
            if pygame.mixer.music.get_busy():
                pygame.mixer.music.stop()

        pygame.display.flip()
        clock.tick(60)

    stop_tcp.set()
    if tcp_thread is not None:
        tcp_thread.join(timeout=1.5)
    pygame.mixer.music.stop()
    pygame.quit()


if __name__ == "__main__":
    main()
