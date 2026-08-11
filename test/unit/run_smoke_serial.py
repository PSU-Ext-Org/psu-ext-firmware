# Copyright 2026 PSU-EXT Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import re
import sys
import time

import serial


def read_until(serial_port, deadline, stop_text):
    chunks = []
    while time.monotonic() < deadline:
        data = serial_port.read(4096)
        if data:
            text = data.decode("utf-8", errors="replace")
            chunks.append(text)
            output = "".join(chunks)
            if stop_text in output:
                return output
        else:
            time.sleep(0.05)
    return "".join(chunks)


def has_unity_summary(output, summary):
    return re.search(r"(?m)^" + re.escape(summary) + r"\s*$", output) is not None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    parser.add_argument("--selector", default="[smoke]")
    parser.add_argument("--expect", default="calibration infrastructure smoke:PASS")
    parser.add_argument("--summary", default="1 Tests 0 Failures 0 Ignored")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    with serial.Serial(args.port, baudrate=args.baud, timeout=0.2, write_timeout=2.0) as ser:
        ser.dtr = False
        ser.rts = False
        time.sleep(2.0)
        ser.reset_input_buffer()

        ser.write(b"\r\n")
        ser.flush()
        time.sleep(0.5)
        ser.write(args.selector.encode("utf-8") + b"\r\n")
        ser.flush()

        output = read_until(
            ser,
            time.monotonic() + args.timeout,
            args.summary,
        )

    print(output)
    if args.expect not in output:
        print(f"missing expected output: {args.expect}", file=sys.stderr)
        return 1
    if not has_unity_summary(output, args.summary):
        print("missing Unity success summary", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
