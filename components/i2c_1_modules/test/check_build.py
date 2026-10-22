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

"""Check optional module exclusion in an ESP-IDF build directory."""

import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--nm", required=True, help="Target toolchain nm executable")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    config = json.loads((build / "config/sdkconfig.json").read_text())
    commands = json.loads((build / "compile_commands.json").read_text())
    files = [entry["file"].replace("\\", "/") for entry in commands]
    elf = next(build.glob("*.elf"))
    symbols = subprocess.check_output([args.nm, str(elf)], text=True)
    image = elf.read_bytes()
    for name, symbol in [("mlx90614", "PSUEXT_I2C_1_MLX90614"),
                         ("ssd1306", "PSUEXT_I2C_1_SSD1306")]:
        enabled = bool(config.get(symbol, False))
        sources = [file for file in files if f"/{name}/" in file]
        assert bool(sources) == enabled, (name, enabled, sources)
        assert (f"{name}_" in symbols) == enabled, (name, "ELF symbols")
        if name == "mlx90614":
            assert (b"EXT:MEAS:TEMP?" in image) == enabled, "Temperature command strings"
        if not enabled:
            for command in commands:
                assert f"/{name}/" not in command["command"].replace("\\", "/"), command
            assert not any(Path(file).name in (f"test_{name}.c", f"test_{name}_views.c",
                                               f"{name}_test_view.c") for file in files)
        print(f"{name}: {'enabled' if enabled else 'excluded'}; sources and ELF verified")


if __name__ == "__main__":
    main()
