#!/usr/bin/env python3
"""Read-only HTTPS WebDAV + byte-range server for VitaWave hardware tests."""

from __future__ import annotations

import argparse
import base64
import hashlib
import hmac
import mimetypes
import os
from pathlib import Path, PurePosixPath
import shutil
import socket
import ssl
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import quote, unquote, urlsplit
import xml.etree.ElementTree as ET


DAV = "DAV:"
ET.register_namespace("d", DAV)


def default_media_dir() -> Path:
    movies = Path.home() / "Movies"
    return movies if movies.is_dir() else Path.cwd()


def lan_ipv4_addresses() -> list[str]:
    addresses: set[str] = {"127.0.0.1"}
    try:
        for entry in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            addresses.add(entry[4][0])
    except socket.gaierror:
        pass
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("192.0.2.1", 9))
        addresses.add(probe.getsockname()[0])
    except OSError:
        pass
    finally:
        probe.close()
    return sorted(addresses, key=lambda value: (value.startswith("127."), value))


def certificate_public_key_pin(openssl: str, certificate: Path) -> str:
    public_key = subprocess.run(
        [openssl, "x509", "-in", str(certificate), "-pubkey", "-noout"],
        check=True,
        capture_output=True,
    ).stdout
    der = subprocess.run(
        [openssl, "pkey", "-pubin", "-outform", "DER"],
        input=public_key,
        check=True,
        capture_output=True,
    ).stdout
    return "sha256//" + base64.b64encode(hashlib.sha256(der).digest()).decode("ascii")


def certificate_has_addresses(openssl: str, certificate: Path,
                              addresses: list[str]) -> bool:
    if not certificate.is_file():
        return False
    result = subprocess.run(
        [openssl, "x509", "-in", str(certificate), "-noout", "-ext",
         "subjectAltName"],
        check=False,
        capture_output=True,
        text=True,
    )
    return result.returncode == 0 and all(
        f"IP Address:{address}" in result.stdout for address in addresses
    )


def ensure_certificate(state_dir: Path, addresses: list[str]) -> tuple[Path, Path, str]:
    openssl = shutil.which("openssl")
    if not openssl:
        raise RuntimeError("openssl non trovato; installalo prima di avviare WebDAV")
    state_dir.mkdir(parents=True, exist_ok=True)
    certificate = state_dir / "webdav-cert.pem"
    private_key = state_dir / "webdav-key.pem"
    required = sorted(set(addresses + ["127.0.0.1"]))
    if not private_key.is_file() or not certificate_has_addresses(
            openssl, certificate, required):
        sans = ["DNS:localhost", f"DNS:{socket.gethostname()}"]
        sans.extend(f"IP:{address}" for address in required)
        temporary_certificate = state_dir / "webdav-cert.pem.new"
        temporary_key = state_dir / "webdav-key.pem.new"
        for path in (temporary_certificate, temporary_key):
            path.unlink(missing_ok=True)
        subprocess.run(
            [openssl, "req", "-x509", "-newkey", "rsa:2048", "-sha256",
             "-nodes", "-days", "3650", "-subj", "/CN=VitaWave local WebDAV",
             "-addext", "subjectAltName=" + ",".join(sans),
             "-keyout", str(temporary_key), "-out", str(temporary_certificate)],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        os.chmod(temporary_key, 0o600)
        temporary_key.replace(private_key)
        temporary_certificate.replace(certificate)
    return certificate, private_key, certificate_public_key_pin(openssl, certificate)


class VitaWaveWebDavHandler(BaseHTTPRequestHandler):
    server_version = "VitaWaveWebDAV/1.0"
    protocol_version = "HTTP/1.1"

    @property
    def media_root(self) -> Path:
        return self.server.media_root  # type: ignore[attr-defined]

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"[{self.client_address[0]}] {fmt % args}")

    def authenticated(self) -> bool:
        expected = "Basic " + base64.b64encode(
            f"{self.server.username}:{self.server.password}".encode("utf-8")
        ).decode("ascii")  # type: ignore[attr-defined]
        supplied = self.headers.get("Authorization", "")
        if hmac.compare_digest(supplied, expected):
            return True
        self.send_response(401)
        self.send_header("WWW-Authenticate", 'Basic realm="VitaWave local test"')
        self.send_header("Content-Length", "0")
        self.end_headers()
        return False

    def local_path(self) -> Path | None:
        raw_path = unquote(urlsplit(self.path).path)
        parts = PurePosixPath(raw_path).parts
        if any(part == ".." or "\x00" in part for part in parts):
            return None
        candidate = self.media_root.joinpath(
            *(part for part in parts if part not in ("/", ""))
        ).resolve()
        try:
            candidate.relative_to(self.media_root)
        except ValueError:
            return None
        return candidate

    def dav_href(self, path: Path) -> str:
        relative = path.relative_to(self.media_root)
        encoded = "/" + "/".join(quote(part, safe="") for part in relative.parts)
        if path.is_dir() and not encoded.endswith("/"):
            encoded += "/"
        return encoded

    def send_missing(self) -> None:
        self.send_response(404)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_OPTIONS(self) -> None:  # noqa: N802
        if not self.authenticated():
            return
        self.send_response(200)
        self.send_header("DAV", "1")
        self.send_header("Allow", "OPTIONS, PROPFIND, HEAD, GET")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_PROPFIND(self) -> None:  # noqa: N802
        try:
            content_length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self.send_error(400, "Content-Length non valido")
            return
        if content_length < 0 or content_length > 1024 * 1024:
            self.send_error(413, "Corpo PROPFIND troppo grande")
            return
        if content_length:
            self.rfile.read(content_length)
        if not self.authenticated():
            return
        target = self.local_path()
        if target is None or not target.exists():
            self.send_missing()
            return
        depth = self.headers.get("Depth", "1")
        if depth not in ("0", "1"):
            self.send_response(403)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        paths = [target]
        if target.is_dir() and depth == "1":
            try:
                paths.extend(sorted(target.iterdir(), key=lambda item: (
                    not item.is_dir(), item.name.casefold())))
            except OSError:
                self.send_missing()
                return
        multistatus = ET.Element(ET.QName(DAV, "multistatus"))
        for path in paths:
            try:
                stat_result = path.stat()
            except OSError:
                continue
            response = ET.SubElement(multistatus, ET.QName(DAV, "response"))
            ET.SubElement(response, ET.QName(DAV, "href")).text = self.dav_href(path)
            propstat = ET.SubElement(response, ET.QName(DAV, "propstat"))
            prop = ET.SubElement(propstat, ET.QName(DAV, "prop"))
            resource_type = ET.SubElement(prop, ET.QName(DAV, "resourcetype"))
            if path.is_dir():
                ET.SubElement(resource_type, ET.QName(DAV, "collection"))
            else:
                ET.SubElement(prop, ET.QName(DAV, "getcontentlength")).text = str(
                    stat_result.st_size)
            ET.SubElement(prop, ET.QName(DAV, "displayname")).text = (
                path.name if path != self.media_root else self.media_root.name)
            ET.SubElement(propstat, ET.QName(DAV, "status")).text = (
                "HTTP/1.1 200 OK")
        payload = ET.tostring(multistatus, encoding="utf-8", xml_declaration=True)
        self.send_response(207)
        self.send_header("Content-Type", "application/xml; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_HEAD(self) -> None:  # noqa: N802
        self.serve_file(send_body=False)

    def do_GET(self) -> None:  # noqa: N802
        self.serve_file(send_body=True)

    def serve_file(self, send_body: bool) -> None:
        if not self.authenticated():
            return
        target = self.local_path()
        if target is None or not target.is_file():
            self.send_missing()
            return
        try:
            size = target.stat().st_size
        except OSError:
            self.send_missing()
            return
        start, end, partial = 0, max(0, size - 1), False
        range_header = self.headers.get("Range")
        if range_header:
            try:
                unit, value = range_header.split("=", 1)
                if unit.strip().lower() != "bytes" or "," in value:
                    raise ValueError
                first, last = value.strip().split("-", 1)
                if first:
                    start = int(first)
                    end = int(last) if last else size - 1
                else:
                    suffix = int(last)
                    start = max(0, size - suffix)
                    end = size - 1
                if start < 0 or end < start or start >= size:
                    raise ValueError
                end = min(end, size - 1)
                partial = True
            except (ValueError, TypeError):
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{size}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
        length = end - start + 1 if size else 0
        self.send_response(206 if partial else 200)
        content_type = mimetypes.guess_type(target.name)[0] or (
            "application/octet-stream")
        self.send_header("Content-Type", content_type)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(length))
        if partial:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        if not send_body or length == 0:
            return
        try:
            with target.open("rb") as media:
                media.seek(start)
                remaining = length
                while remaining:
                    block = media.read(min(256 * 1024, remaining))
                    if not block:
                        break
                    self.wfile.write(block)
                    remaining -= len(block)
        except (BrokenPipeError, ConnectionResetError):
            pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--media-dir", type=Path, default=default_media_dir())
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument("--username", default="vitawave")
    parser.add_argument("--password", default="vitawave")
    parser.add_argument(
        "--state-dir", type=Path,
        default=Path.home() / "Library/Caches/VitaWave/local-streaming/webdav",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.media_dir.expanduser().resolve()
    if not root.is_dir():
        print(f"Cartella media non valida: {root}", file=sys.stderr)
        return 2
    if not (1 <= args.port <= 65535):
        print("La porta deve essere compresa tra 1 e 65535", file=sys.stderr)
        return 2
    addresses = lan_ipv4_addresses()
    if args.bind not in ("0.0.0.0", "127.0.0.1", "localhost"):
        addresses.append(args.bind)
    try:
        certificate, private_key, pin = ensure_certificate(
            args.state_dir.expanduser(), addresses)
    except (OSError, subprocess.CalledProcessError, RuntimeError) as error:
        print(f"Impossibile preparare TLS: {error}", file=sys.stderr)
        return 1
    server = ThreadingHTTPServer((args.bind, args.port), VitaWaveWebDavHandler)
    server.daemon_threads = True
    server.media_root = root  # type: ignore[attr-defined]
    server.username = args.username  # type: ignore[attr-defined]
    server.password = args.password  # type: ignore[attr-defined]
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(certificate, private_key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    host = args.bind if args.bind not in ("0.0.0.0", "localhost") else next(
        (item for item in addresses if not item.startswith("127.")), "127.0.0.1")
    print("\nWebDAV HTTPS VitaWave avviato")
    print(f"  Media:      {root}")
    print(f"  URL:        https://{host}:{args.port}/")
    print(f"  Utente:     {args.username}")
    print(f"  Password:   {args.password}")
    print(f"  Pin TLS:    {pin}")
    print("\nIn VitaWave: WebDAV, host = IP del Mac, porta = quella sopra, ")
    print("percorso iniziale vuoto. Al primo accesso confronta e conferma il pin TLS.")
    print("Interrompi il server con Ctrl-C.\n")
    try:
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        print("\nArresto WebDAV...")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
