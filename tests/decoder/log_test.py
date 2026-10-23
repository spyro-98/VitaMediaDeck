import importlib.util
from pathlib import Path
spec = importlib.util.spec_from_file_location("decoder_log", Path(__file__).parents[2] / "tools/analyze-decoder-log.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
rows = [
    "video clocks: profile=APP_FIXED highfps=1 ARM 444->333 ret=0 BUS 222->166 ret=0 GPU 222->166 ret=0 XBAR 111->111 ret=0",
    "software video: H.264 ready backend=H264_VITA_HW 1280x720 60fps texture=1280x720 pitch=1280 memory=30720 KiB queue=16 mode=HW_DIRECT_NV12 thread=1 active=0x0 highfps=1",
    "software video pacing: nominal=60 fps shown=59.9 fps media/wall=1.000x lag=10 ms av=-5 ms decoded=310 shown_total=300 drop=0 late=0 catchup=0 pts_fix=0 cadence_break=0 gap_max=17 ms",
    "software video: thread end pkt=312 frame=310 decode=5100 ms copy=0 ms work=680 ms queue_wait=4400 ms surface_wait=20 ms late_drop=0 catchup_switch=0 pts_fix=0 missing=0 nonmono=0 zero=1 eof=0 err=0",
    "Authorization: NEVER_CAPTURE_THIS",
]
r = module.parse(rows)
assert r["clocks"][0]["arm"]["actual_mhz"] == 333
assert r["opens"][0]["surface_memory_kib"] == 30720
assert r["pacing"][0]["shown"] == 59.9 and r["pacing"][0]["av"] == -5
assert r["stops"][0]["work"] == 680 and r["stops"][0]["queue_wait"] == 4400
assert not r["energy_measured"] and "NEVER_CAPTURE_THIS" not in str(r)
assert all(not v for k, v in module.parse([]).items() if isinstance(v, list))
print("Decoder log extraction: clocks, format, cadence, active/wait distinction, empty logs and numeric whitelist passed.")

# Actual console records used to concatenate highfps and the next producer.
interleaved = module.parse([rows[1].replace("highfps=1", "highfps=0vita_aac: module loaded")])
assert interleaved["opens"][0]["mode"] == "HW_DIRECT_NV12"
assert "highfps" not in interleaved["opens"][0]
assert module.parse(["software video pacing: shown=59.9.1 lag=--5 av=+"])["pacing"] == []
tagged = module.parse(["build: id=playerfix-1023 bubble=VMD playerfix-1023 sw_upload=NV12_P2", rows[0] + " requested_ARM=444 requested_BUS=222 requested_GPU=222 requested_XBAR=111"])
assert tagged["builds"][0]["id"] == "playerfix-1023"
assert tagged["clocks"][0]["arm"]["requested_mhz"] == 444
assert tagged["clocks"][0]["arm"]["actual_mhz"] == 333
print("Console interleaving, malformed numeric fields, build identity and requested/actual clocks passed.")

joined = module.parse([rows[2] + rows[3] + "software video: join shown=128 gap_max=21000" + rows[2]])
assert len(joined["pacing"]) == 2 and all(p["shown"] == 59.9 and p["gap_max"] == 17 for p in joined["pacing"])
assert len(joined["stops"]) == 1 and joined["stops"][0]["frame"] == 310
print("Multiple concatenated events preserve video FPS and GPU gaps without importing join/audio fields.")

legacy = module.parse([rows[3].replace(" work=680 ms", "")])
assert len(legacy["stops"]) == 1 and "work" not in legacy["stops"][0]
