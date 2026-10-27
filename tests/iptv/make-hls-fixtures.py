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

# Match the failing live class: progressive, one-reference H.264 without B-frames.
ffmpeg("-f", "lavfi", "-i", "testsrc2=size=640x360:rate=25", "-f", "lavfi", "-i",
       "sine=frequency=440:sample_rate=48000", "-t", "12", "-c:v", "libx264", "-preset", "ultrafast",
       "-pix_fmt", "yuv420p", "-g", "50", "-bf", "0", "-refs", "1", "-c:a", "aac", "-ac", "2", root / "nodelay.mp4")
for name,extra in [("nodelay",[]),("nodelay-mux",["-hls_segment_type","fmp4"])]:
    ffmpeg("-i",root/"nodelay.mp4","-map","0:v","-map","0:a","-c","copy","-hls_time","2",
           "-hls_playlist_type","vod","-hls_fmp4_init_filename",name+"-init.mp4",*extra,root/(name+".m3u8"))

# PTS-only MPEG-TS with non-pyramid B-frames: matches the observed broadcaster
# timestamp class using locally generated media, never captured broadcasts.
ffmpeg("-f", "lavfi", "-i", "testsrc2=size=512x288:rate=50", "-f", "lavfi", "-i",
       "sine=frequency=440:sample_rate=48000", "-t", "12", "-c:v", "libx264", "-preset", "ultrafast",
       "-pix_fmt", "yuv420p", "-g", "100", "-bf", "2", "-x264-params", "b-pyramid=none:b-adapt=0",
       "-c:a", "aac", "-ac", "2", "-f", "hls", "-hls_time", "2", "-hls_playlist_type", "vod", root / "pts-only.m3u8")
# A second fixture retains the encoder's mixture of PTS-only and PTS+DTS PES,
# shifting explicit video DTS forward by one frame (20 ms at 90 kHz).
(root / "shifted-dts.m3u8").write_text((root / "pts-only.m3u8").read_text().replace("pts-only", "shifted-dts"))
pts_only_headers = 0
shifted_headers = 0
for path in root.glob("pts-only*.ts"):
    body = bytearray(path.read_bytes())
    shifted = bytearray(body)
    for offset in range(0, len(body), 188):
        assert body[offset] == 0x47
        if not body[offset+1] & 64:
            continue
        adaptation = (body[offset+3] >> 4) & 3
        if not adaptation & 1:
            continue
        pes = offset+4+(1+body[offset+4] if adaptation & 2 else 0)
        if pes+19 > offset+188 or body[pes:pes+3] != b"\x00\x00\x01" or not 0xe0 <= body[pes+3] <= 0xef:
            continue
        if body[pes+7] >> 6 == 3:
            encoded = body[pes+14:pes+19]
            dts = (((encoded[0] >> 1) & 7) << 30) | (encoded[1] << 22) | ((encoded[2] >> 1) << 15) | (encoded[3] << 7) | (encoded[4] >> 1)
            dts = (dts + 1800) & ((1 << 33) - 1)
            shifted[pes+14:pes+19] = bytes((0x11 | ((dts >> 29) & 14), (dts >> 22) & 255, 1 | ((dts >> 14) & 254), (dts >> 7) & 255, 1 | ((dts << 1) & 254)))
            shifted_headers += 1
            # Keep the PES length and header length: replace DTS with legal
            # stuffing. PTS uses the PTS-only prefix; compressed bytes are intact.
            body[pes+7] = (body[pes+7] & 63) | 128
            body[pes+9] = (body[pes+9] & 15) | 32
            body[pes+14:pes+19] = b"\xff" * 5
            pts_only_headers += 1
    path.write_bytes(body)
    path.with_name(path.name.replace("pts-only", "shifted-dts")).write_bytes(shifted)
assert shifted_headers > 100
assert pts_only_headers > 100
print("Synthetic PTS-only video PES headers:", pts_only_headers)
