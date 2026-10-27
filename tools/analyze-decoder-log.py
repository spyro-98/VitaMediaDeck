#!/usr/bin/env python3
"""Extract measured decoder events, never infer Vita energy from CPU time.

python3 tools/analyze-decoder-log.py session.log > decoder.json
"""
import argparse
import json
import re
from pathlib import Path


def parse(lines):
    result = {"clocks": [], "opens": [], "pacing": [], "stops": [], "builds": [],
              "decoder_clocks": [], "energy_measured": False}
    # Old console logs concatenate records. Parse complete event grammars and
    # every occurrence; never let a later join's shown= or AAC gap_max= replace
    # the video cadence fields from the beginning of the same physical line.
    clock = re.compile(r"video clocks: profile=(?P<owner>\w+) highfps=\d+ " +
                       r"ARM (?P<arm_before>-?\d+)->(?P<arm_actual>-?\d+) ret=[0-9A-Fa-f]+ " +
                       r"BUS (?P<bus_before>-?\d+)->(?P<bus_actual>-?\d+) ret=[0-9A-Fa-f]+ " +
                       r"GPU (?P<gpu_before>-?\d+)->(?P<gpu_actual>-?\d+) ret=[0-9A-Fa-f]+ " +
                       r"XBAR (?P<xbar_before>-?\d+)->(?P<xbar_actual>-?\d+) ret=[0-9A-Fa-f]+" +
                       r"(?: requested_ARM=(?P<arm_request>\d+) requested_BUS=(?P<bus_request>\d+) requested_GPU=(?P<gpu_request>\d+) requested_XBAR=(?P<xbar_request>\d+))?")
    decoder_clock = re.compile(r"video clocks: backend=(?P<backend>HW|SW) requested_CPU=(?P<requested>\d+) actual_CPU=(?P<actual>\d+) owner=(?P<owner>app|external)(?=\s|$)")
    opened = re.compile(r"software video: H\.264 ready backend=(?P<backend>\w+) " +
                        r"(?P<width>\d+)x(?P<height>\d+) (?P<fps>\d+)fps texture=\d+x\d+ pitch=\d+ " +
                        r"memory=(?P<surface_memory_kib>\d+) KiB queue=(?P<queue>\d+) mode=(?P<mode>\w+) " +
                        r"thread=(?P<thread>\d+) active=0x[0-9A-Fa-f]+(?: highfps=(?P<highfps>[01])(?=\s|$))?")
    cadence = re.compile(r"software video pacing: nominal=(?P<nominal>\d+) fps shown=(?P<shown>\d+\.\d+) fps " +
                         r"media/wall=(?P<media_wall>\d+\.\d+)x lag=(?P<lag>\d+) ms av=(?P<av>[+-]?\d+) ms " +
                         r"decoded=(?P<decoded>\d+) shown_total=(?P<shown_total>\d+) drop=(?P<drop>\d+) " +
                         r"late=(?P<late>\d+) catchup=(?P<catchup>\d+) pts_fix=(?P<pts_fix>\d+) " +
                         r"cadence_break=(?P<cadence_break>\d+) gap_max=(?P<gap_max>\d+) ms")
    keys = ("pkt", "frame", "decode", "copy", "work", "queue_wait", "surface_wait", "late_drop",
            "catchup_switch", "pts_fix", "missing", "nonmono", "zero", "eof", "err")
    stopped = re.compile("software video: thread end" + "".join(
        (rf"(?: work=(?P<work>-?\d+) ms)?" if key == "work" else
         rf" {key}=(?P<{key}>-?\d+)" + (" ms" if key in ("decode", "copy", "queue_wait", "surface_wait") else ""))
        for key in keys))
    for number, line in enumerate(lines, 1):
        for m in re.finditer(r"build: id=([A-Za-z0-9_-]+)(?=\s|$)", line):
            result["builds"].append({"line": number, "id": m[1]})
        for m in clock.finditer(line):
            entry = {"line": number, "owner": m["owner"]}
            for key in ("arm", "bus", "gpu", "xbar"):
                entry[key] = {"before_mhz": int(m[key + "_before"]), "actual_mhz": int(m[key + "_actual"])}
                if m[key + "_request"] is not None:
                    entry[key]["requested_mhz"] = int(m[key + "_request"])
            result["clocks"].append(entry)
        for m in decoder_clock.finditer(line):
            result["decoder_clocks"].append({"line": number, "backend": m["backend"], "requested_mhz": int(m["requested"]), "actual_mhz": int(m["actual"]), "owner": m["owner"]})
        for m in opened.finditer(line):
            values = {key: value if key in ("backend", "mode") else int(value)
                      for key, value in m.groupdict().items() if value is not None}
            result["opens"].append({"line": number, **values})
        for m in cadence.finditer(line):
            values = {key: float(value) if key in ("shown", "media_wall") else int(value)
                      for key, value in m.groupdict().items()}
            result["pacing"].append({"line": number, **values})
        for m in stopped.finditer(line):
            values = {key: int(value) for key, value in m.groupdict().items() if value is not None}
            result["stops"].append({"line": number, **values, "timings_unit": "milliseconds",
                                    "decode_includes_copy_and_wait": True})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    with args.log.open(encoding="utf-8", errors="replace") as source:
        print(json.dumps(parse(source), indent=2))


if __name__ == "__main__":
    main()
