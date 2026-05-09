#!/usr/bin/env python3
"""
TCP server for PC: listens for text lines from STM32 (e.g. A0_avg=...\\r\\n).
Board must use the same port as APP_WIFI_REMOTE_PORT in Core/Inc/main.h (default 8002).

Usage:
  python pc_adc_tcp_server.py
  python pc_adc_tcp_server.py --port 8002
"""

from __future__ import annotations

import argparse
import socket
import sys


def main() -> None:
    parser = argparse.ArgumentParser(description="Receive ADC lines from STM32 over TCP")
    parser.add_argument(
        "--host",
        default="0.0.0.0",
        help="Bind address (0.0.0.0 = all interfaces)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=8002,
        help="Must match APP_WIFI_REMOTE_PORT in main.h",
    )
    args = parser.parse_args()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        srv.bind((args.host, args.port))
    except OSError as e:
        print(f"Bind failed ({args.host}:{args.port}): {e}", flush=True)
        print("Another program may use this port, or try --port with a different value.", flush=True)
        sys.exit(1)

    srv.listen(1)
    print(f"Listening on {args.host}:{args.port} (board connects here as TCP client).", flush=True)
    while True:
        conn, addr = srv.accept()
        peer = f"{addr[0]}:{addr[1]}"
        print(f">>> Accepted connection from {peer}", flush=True)
        try:
            buf = b""
            while True:
                chunk = conn.recv(4096)
                if not chunk:
                    print(f">>> Peer {peer} closed (no data).", flush=True)
                    break
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("utf-8", errors="replace").strip()
                    if text:
                        print(text, flush=True)
        except OSError as e:
            print(f"Socket error with {peer}: {e}", flush=True)
        finally:
            conn.close()
            print(f">>> Connection {peer} closed. Waiting for next client...\n", flush=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.", flush=True)
        sys.exit(0)
