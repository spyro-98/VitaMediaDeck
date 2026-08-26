#!/usr/bin/env python3
"""Read-only authenticated SMB2 server for VitaWave hardware tests."""

from __future__ import annotations

import argparse
from importlib.metadata import PackageNotFoundError, version
import logging
from pathlib import Path
import socket
import sys


def default_media_dir() -> Path:
    movies = Path.home() / "Movies"
    return movies if movies.is_dir() else Path.cwd()


def lan_ipv4_address() -> str:
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("192.0.2.1", 9))
        return probe.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        probe.close()


def load_impacket():
    try:
        installed = version("impacket")
        from impacket import smbserver  # type: ignore
        from impacket.ntlm import compute_lmhash, compute_nthash  # type: ignore
    except (ImportError, PackageNotFoundError):
        print("Dipendenza mancante. Esegui:", file=sys.stderr)
        print("  python3 -m pip install 'impacket==0.13.1'", file=sys.stderr)
        raise SystemExit(2)
    numeric = tuple(int(part) for part in installed.split(".")[:3]
                    if part.isdigit())
    if numeric < (0, 13, 1):
        print("Serve Impacket 0.13.1 o successivo per la firma SMB2.",
              file=sys.stderr)
        raise SystemExit(2)
    return smbserver, compute_lmhash, compute_nthash, installed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--media-dir", type=Path, default=default_media_dir())
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=1445)
    parser.add_argument("--share", default="VITAWAVE")
    parser.add_argument("--username", default="vitawave")
    parser.add_argument("--password", default="vitawave")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    smbserver, compute_lmhash, compute_nthash, installed = load_impacket()
    root = args.media_dir.expanduser().resolve()
    if not root.is_dir() or not (1 <= args.port <= 65535):
        print("Cartella media o porta non valida", file=sys.stderr)
        return 2
    share = args.share.strip().upper()
    if not share or any(character in share for character in "\\/[]:;|=,+*?<>\""):
        print("Nome condivisione SMB non valido", file=sys.stderr)
        return 2
    # Impacket's INFO log includes the NTLM challenge/response material. The
    # local test server keeps that diagnostic output disabled by default.
    logging.basicConfig(level=logging.WARNING,
                        format="[%(levelname)s] %(message)s")
    server = smbserver.SimpleSMBServer(
        listenAddress=args.bind, listenPort=args.port)
    server.addShare(share, str(root), "VitaWave local media", readOnly="yes")
    server.setSMB2Support(True)
    server.setNTLMSupport(True)
    server.setKerberosSupport(False)
    server.addCredential(
        args.username, 0, compute_lmhash(args.password),
        compute_nthash(args.password))
    host = args.bind if args.bind not in ("0.0.0.0", "localhost") \
        else lan_ipv4_address()
    print("\nSMB2 VitaWave avviato")
    print(f"  Media:          {root}")
    print(f"  Endpoint:       {host}:{args.port}")
    print(f"  Condivisione:   {share}")
    print(f"  Utente:         {args.username}")
    print(f"  Password:       {args.password}")
    print(f"  Impacket:       {installed}")
    print("\nIn VitaWave: SMB, host = IP del Mac, porta = quella sopra, ")
    print("condivisione = nome sopra, percorso iniziale vuoto, dominio vuoto.")
    print("Il server e' in sola lettura e firma le sessioni autenticate.")
    print("Interrompi il server con Ctrl-C.\n")
    try:
        server.start()
    except KeyboardInterrupt:
        print("\nArresto SMB...")
        server.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
