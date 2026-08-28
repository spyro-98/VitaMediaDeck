#!/usr/bin/env python3
"""Read-only password-authenticated SFTP server for VitaMediaDeck hardware tests."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import socket
import sys
import threading
import time


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


def load_paramiko():
    try:
        import paramiko  # type: ignore
        return paramiko
    except ImportError:
        print("Dipendenza mancante. Esegui:", file=sys.stderr)
        print("  python3 -m pip install 'paramiko>=3.5,<5'", file=sys.stderr)
        raise SystemExit(2)


def server_types(paramiko, media_root: Path, username: str, password: str):
    class PasswordServer(paramiko.ServerInterface):
        def check_auth_password(self, supplied_user, supplied_password):
            if supplied_user == username and supplied_password == password:
                return paramiko.AUTH_SUCCESSFUL
            return paramiko.AUTH_FAILED

        def get_allowed_auths(self, supplied_user):
            del supplied_user
            return "password"

        def check_channel_request(self, kind, chanid):
            del chanid
            return (paramiko.OPEN_SUCCEEDED if kind == "session"
                    else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED)

    class ReadOnlySftp(paramiko.SFTPServerInterface):
        def _path(self, remote_path: str) -> Path | None:
            pure = PurePosixPath(remote_path)
            if any(part == ".." or "\x00" in part for part in pure.parts):
                return None
            candidate = media_root.joinpath(
                *(part for part in pure.parts if part not in ("/", ""))
            ).resolve()
            try:
                candidate.relative_to(media_root)
            except ValueError:
                return None
            return candidate

        @staticmethod
        def _error(error: OSError) -> int:
            return paramiko.SFTPServer.convert_errno(error.errno or 1)

        def canonicalize(self, path):
            candidate = self._path(path)
            if candidate is None:
                return "/"
            relative = candidate.relative_to(media_root)
            return "/" + relative.as_posix() if relative.parts else "/"

        def list_folder(self, path):
            candidate = self._path(path)
            if candidate is None:
                return paramiko.SFTP_PERMISSION_DENIED
            try:
                result = []
                for child in sorted(candidate.iterdir(), key=lambda item: (
                        not item.is_dir(), item.name.casefold())):
                    attributes = paramiko.SFTPAttributes.from_stat(child.stat())
                    attributes.filename = child.name
                    result.append(attributes)
                return result
            except OSError as error:
                return self._error(error)

        def stat(self, path):
            candidate = self._path(path)
            if candidate is None:
                return paramiko.SFTP_PERMISSION_DENIED
            try:
                return paramiko.SFTPAttributes.from_stat(candidate.stat())
            except OSError as error:
                return self._error(error)

        lstat = stat

        def open(self, path, flags, attr):
            del attr
            if (flags & os.O_ACCMODE) != os.O_RDONLY or flags & (
                    os.O_CREAT | os.O_TRUNC | os.O_APPEND):
                return paramiko.SFTP_PERMISSION_DENIED
            candidate = self._path(path)
            if candidate is None or not candidate.is_file():
                return paramiko.SFTP_NO_SUCH_FILE
            try:
                handle = paramiko.SFTPHandle(flags)
                handle.readfile = candidate.open("rb")
                return handle
            except OSError as error:
                return self._error(error)

        def remove(self, path):
            del path
            return paramiko.SFTP_PERMISSION_DENIED

        def rename(self, oldpath, newpath):
            del oldpath, newpath
            return paramiko.SFTP_PERMISSION_DENIED

        def mkdir(self, path, attr):
            del path, attr
            return paramiko.SFTP_PERMISSION_DENIED

        def rmdir(self, path):
            del path
            return paramiko.SFTP_PERMISSION_DENIED

        def chattr(self, path, attr):
            del path, attr
            return paramiko.SFTP_PERMISSION_DENIED

    return PasswordServer, ReadOnlySftp


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--media-dir", type=Path, default=default_media_dir())
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=2222)
    parser.add_argument("--username", default="vitamediadeck")
    parser.add_argument("--password", default="vitamediadeck")
    parser.add_argument(
        "--state-dir", type=Path,
        default=Path.home() / "Library/Caches/VitaMediaDeck/local-streaming/sftp",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    paramiko = load_paramiko()
    root = args.media_dir.expanduser().resolve()
    if not root.is_dir() or not (1 <= args.port <= 65535):
        print("Cartella media o porta non valida", file=sys.stderr)
        return 2
    state_dir = args.state_dir.expanduser()
    state_dir.mkdir(parents=True, exist_ok=True)
    key_path = state_dir / "sftp-host-key.pem"
    if key_path.is_file():
        host_key = paramiko.RSAKey.from_private_key_file(str(key_path))
    else:
        host_key = paramiko.RSAKey.generate(2048)
        host_key.write_private_key_file(str(key_path))
        os.chmod(key_path, 0o600)
    digest = hashlib.sha256(host_key.asbytes()).hexdigest().upper()
    fingerprint = ":".join(digest[index:index + 2]
                           for index in range(0, len(digest), 2))
    PasswordServer, ReadOnlySftp = server_types(
        paramiko, root, args.username, args.password)
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((args.bind, args.port))
    listener.listen(16)
    listener.settimeout(0.5)
    host = args.bind if args.bind not in ("0.0.0.0", "localhost") \
        else lan_ipv4_address()
    print("\nSFTP VitaMediaDeck avviato")
    print(f"  Media:       {root}")
    print(f"  Endpoint:    {host}:{args.port}")
    print(f"  Utente:      {args.username}")
    print(f"  Password:    {args.password}")
    print(f"  Fingerprint: {fingerprint}")
    print("\nIn VitaMediaDeck: SFTP, host = IP del Mac, porta = quella sopra, ")
    print("percorso iniziale = /. Confronta la fingerprint al primo accesso.")
    print("Interrompi il server con Ctrl-C.\n")

    def serve_client(client: socket.socket, address) -> None:
        transport = paramiko.Transport(client)
        try:
            transport.add_server_key(host_key)
            transport.set_subsystem_handler(
                "sftp", paramiko.SFTPServer, ReadOnlySftp)
            transport.start_server(server=PasswordServer())
            print(f"[{address[0]}] connessione SFTP aperta")
            while transport.is_active():
                time.sleep(0.2)
        except Exception as error:  # Paramiko owns protocol-level exceptions.
            print(f"[{address[0]}] errore SFTP: {error}", file=sys.stderr)
        finally:
            transport.close()
            client.close()

    try:
        while True:
            try:
                client, address = listener.accept()
            except socket.timeout:
                continue
            threading.Thread(target=serve_client, args=(client, address),
                             daemon=True).start()
    except KeyboardInterrupt:
        print("\nArresto SFTP...")
    finally:
        listener.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
