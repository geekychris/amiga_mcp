#!/usr/bin/env python3
"""
pyperf.py — Lightweight iperf-like network throughput benchmark tool in pure Python 3.

Works out of the box with python3-amigaos4 and Linux/Windows host!

Usage:
  Server (Listen for incoming throughput test):
    python3 pyperf.py --server [--port 17778] [--bind 0.0.0.0]

  Client (Transmit throughput test):
    python3 pyperf.py --client --host 192.168.100.1 [--port 17778] [--time 10] [--bufsize 65536]
"""

import sys
import time
import socket
import argparse

def run_server(bind_ip="0.0.0.0", port=17778, bufsize=65536):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((bind_ip, port))
    srv.listen(1)

    print(f"=== pyperf Server Listening on {bind_ip}:{port} ===")
    while True:
        try:
            conn, addr = srv.accept()
            print(f"Accepted connection from {addr[0]}:{addr[1]}")
            total_bytes = 0
            start_time = time.time()
            last_report = start_time

            while True:
                data = conn.recv(bufsize)
                if not data:
                    break
                total_bytes += len(data)

            end_time = time.time()
            duration = max(end_time - start_time, 0.001)
            mbps = (total_bytes * 8) / (duration * 1_000_000)
            mb_s = total_bytes / (duration * 1_048_576)

            print(f"=== TEST COMPLETE ===")
            print(f"Received: {total_bytes / (1024*1024):.2f} MB in {duration:.2f} seconds")
            print(f"Throughput: {mbps:.2f} Mbit/sec ({mb_s:.2f} MB/sec)")
            print("--------------------------------------------------")
            conn.close()
        except KeyboardInterrupt:
            print("\nServer shutting down.")
            break
        except Exception as e:
            print(f"Error handling connection: {e}")

    srv.close()

def run_client(host="127.0.0.1", port=17778, duration_sec=10, bufsize=65536):
    print(f"=== pyperf Client Connecting to {host}:{port} for {duration_sec}s ===")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((host, port))
    except Exception as e:
        print(f"Failed to connect to {host}:{port}: {e}")
        return

    payload = b"X" * bufsize
    total_bytes = 0
    start_time = time.time()
    end_time = start_time + duration_sec

    while time.time() < end_time:
        sent = s.send(payload)
        total_bytes += sent

    actual_end = time.time()
    duration = max(actual_end - start_time, 0.001)
    s.close()

    mbps = (total_bytes * 8) / (duration * 1_000_000)
    mb_s = total_bytes / (duration * 1_048_576)

    print(f"=== TEST COMPLETE ===")
    print(f"Transmitted: {total_bytes / (1024*1024):.2f} MB in {duration:.2f} seconds")
    print(f"Throughput: {mbps:.2f} Mbit/sec ({mb_s:.2f} MB/sec)")

def main():
    parser = argparse.ArgumentParser(description="Lightweight Python 3 iperf-like throughput benchmark")
    parser.add_argument("--server", action="store_true", help="Run in server mode")
    parser.add_argument("--client", action="store_true", help="Run in client mode")
    parser.add_argument("--host", default="127.0.0.1", help="Target host IP for client mode")
    parser.add_argument("--port", type=int, default=17778, help="TCP port (default 17778)")
    parser.add_argument("--bind", default="0.0.0.0", help="Bind IP for server mode")
    parser.add_argument("--time", type=int, default=10, help="Test duration in seconds (default 10)")
    parser.add_argument("--bufsize", type=int, default=65536, help="Buffer size in bytes (default 65536)")

    args = parser.parse_args()

    if args.server:
        run_server(bind_ip=args.bind, port=args.port, bufsize=args.bufsize)
    elif args.client:
        run_client(host=args.host, port=args.port, duration_sec=args.time, bufsize=args.bufsize)
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
