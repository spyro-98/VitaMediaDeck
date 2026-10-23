"""Generate private deterministic HLS fixtures; no provider media/keys involved.

Usage: python3 make-hls-fixtures.py NEW_EMPTY_DIRECTORY
Requires ffmpeg and openssl. Native ffmpeg makes the containers; OpenSSL (not
the production decryptor) makes AES-CBC fixtures with independently chosen IVs.
"""
import pathlib
import subprocess
import sys

root = pathlib.Path(sys.argv[1]).resolve()
if not root.is_dir() or any(root.iterdir()):
    raise SystemExit("Pass a new empty temporary fixture directory")

def ffmpeg(*args):
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", *map(str, args)], check=True)

ffmpeg("-f", "lavfi", "-i", "testsrc2=size=640x360:rate=30", "-f", "lavfi", "-i",
       "sine=frequency=440:sample_rate=48000", "-t", "12", "-c:v", "libx264", "-preset", "ultrafast",
       "-pix_fmt", "yuv420p", "-g", "60", "-bf", "2", "-c:a", "aac", "-ac", "2", root / "source.mp4")
for name, maps, extra in [
    ("plain", ["-map", "0:v", "-map", "0:a"], []),
    ("mux", ["-map", "0:v", "-map", "0:a"], ["-hls_segment_type", "fmp4"]),
    ("negative", ["-map", "0:v", "-map", "0:a"], ["-hls_segment_type", "fmp4", "-hls_segment_options", "movflags=+negative_cts_offsets"]),
    ("video", ["-map", "0:v"], ["-hls_segment_type", "fmp4"]),
    ("audio", ["-map", "0:a"], ["-hls_segment_type", "fmp4"]),
    ("range", ["-map", "0:v", "-map", "0:a"], ["-hls_flags", "single_file"]),
    ("maprange", ["-map", "0:v", "-map", "0:a"], ["-hls_flags", "single_file", "-hls_segment_type", "fmp4"]),
]:
    ffmpeg("-i", root / "source.mp4", *maps, "-c", "copy", "-hls_time", "2", "-hls_playlist_type", "vod",
           "-hls_fmp4_init_filename", name + "-init.mp4", *extra, root / (name + ".m3u8"))

master = '''#EXTM3U
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aac",NAME="Italiano",DEFAULT=YES,AUTOSELECT=YES,URI="audio.m3u8"
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aac",NAME="unused",DEFAULT=NO,URI="must-not-fetch.m3u8"
#EXT-X-STREAM-INF:BANDWIDTH=10000000,RESOLUTION=1920x1080,CODECS="avc1.4d401f,mp4a.40.2"
must-not-fetch.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=128000,CODECS="mp4a.40.2",AUDIO="aac"
must-not-fetch-audio.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=1000000,RESOLUTION=640x360,CODECS="avc1.4d401f,mp4a.40.2",AUDIO="aac"
video.m3u8
'''
(root / "separate.m3u8").write_text(master)
plain = (root / "plain.m3u8").read_text().splitlines()
for mode in ("implicit", "explicit", "rotation"):
    lines, sequence = [], 0
    for line in plain:
        if line and not line.startswith("#"):
            key_index = sequence if mode == "rotation" else 0
            key = bytes((i + key_index + 1) % 256 for i in range(16))
            iv = (sequence if mode == "implicit" else 0x123456 + sequence).to_bytes(16, "big")
            key_name = f"key-{key_index}.bin"
            (root / key_name).write_bytes(key)
            suffix = "" if mode == "implicit" else f",IV=0x{iv.hex()}"
            # Exercise METHOD=NONE followed by a new encryption scope as well.
            if mode == "rotation" and sequence == 3:
                lines += ["#EXT-X-KEY:METHOD=NONE", line]
            else:
                lines.append(f'#EXT-X-KEY:METHOD=AES-128,URI="{key_name}"{suffix}')
                dest = f"{mode}-{sequence}.ts"
                subprocess.run(["openssl", "enc", "-aes-128-cbc", "-K", key.hex(), "-iv", iv.hex(),
                                "-in", str(root / line), "-out", str(root / dest)], check=True)
                lines.append(dest)
            sequence += 1
        else:
            lines.append(line)
    (root / (mode + ".m3u8")).write_text("\n".join(lines) + "\n")
print(root)
