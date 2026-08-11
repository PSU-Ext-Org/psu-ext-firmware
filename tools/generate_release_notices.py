#!/usr/bin/env python3
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

"""Assemble the license and notice files that accompany a firmware release."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_file(path: Path) -> Path:
    if not path.is_file():
        raise FileNotFoundError(f"Required license source is missing: {path}")
    return path


def linked_libraries(build_dir: Path) -> tuple[Path, list[str]]:
    link_map = require_file(build_dir / "psu-ext.map")
    map_text = link_map.read_text(encoding="utf-8", errors="replace")
    libraries = sorted(set(re.findall(r"lib([A-Za-z0-9_.-]+)\.a", map_text)))
    return link_map, libraries


def linked_components(build_dir: Path, libraries: list[str]) -> list[dict[str, str]]:
    metadata_path = require_file(build_dir / "project_description.json")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    components = metadata.get("build_component_info", {})
    linked: list[dict[str, str]] = []
    for name, component in components.items():
        archive = component.get("file")
        directory = component.get("dir")
        if not archive or not directory:
            continue
        archive_name = Path(archive).name
        match = re.fullmatch(r"lib(.+)\.a", archive_name)
        if match is None or match.group(1) not in libraries:
            continue
        linked.append(
            {
                "component": name,
                "library": match.group(1),
                "source_directory": directory,
                "source_files": component.get("sources", []),
            }
        )
    return sorted(linked, key=lambda component: component["library"])


def find_newlib_notice(tools_path: Path) -> Path:
    candidates = sorted(tools_path.glob("xtensa-esp-elf/*/xtensa-esp-elf/share/licenses/binutils/COPYING.NEWLIB"))
    if len(candidates) != 1:
        raise FileNotFoundError(
            "Expected exactly one Xtensa toolchain Newlib notice under "
            f"{tools_path / 'xtensa-esp-elf'}; found {len(candidates)}"
        )
    return candidates[0]


def copy_notice(source: Path, destination: Path, component: dict[str, str] | None = None) -> dict[str, str]:
    require_file(source)
    shutil.copyfile(source, destination)
    notice = {"source": str(source), "sha256": sha256(destination)}
    if component is not None:
        notice["component"] = component["component"]
        notice["library"] = component["library"]
    return notice


def license_files_for_component(component: dict[str, object]) -> list[Path]:
    component_root = Path(str(component["source_directory"])).resolve()
    files: set[Path] = set()
    for source_name in component["source_files"]:
        source = Path(str(source_name)).resolve()
        if not source.is_file():
            continue
        directory = source.parent
        while directory == component_root or component_root in directory.parents:
            for candidate in directory.iterdir():
                if candidate.is_file() and candidate.name.upper().startswith(("LICENSE", "NOTICE", "COPYING")):
                    files.add(candidate)
            if directory == component_root:
                break
            directory = directory.parent
    return sorted(files)


def source_header_notices(component: dict[str, object]) -> list[tuple[Path, str]]:
    notices: list[tuple[Path, str]] = []
    for source_name in component["source_files"]:
        source = Path(str(source_name)).resolve()
        if not source.is_file():
            continue
        content = source.read_text(encoding="utf-8", errors="replace")[:4096]
        if not (content.startswith("/*") and "Copyright" in content and "Redistribution" in content):
            continue
        end = content.find("*/")
        if end >= 0:
            notices.append((source, content[: end + 2] + "\n"))
    return notices


def notice_name(component: dict[str, str], source: Path) -> str:
    safe_component = re.sub(r"[^A-Za-z0-9_.-]", "-", component["component"])
    return f"{safe_component}-{sha256(source)[:12]}-{source.name}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"), help="ESP-IDF build directory")
    parser.add_argument("--output", type=Path, default=Path("release-notices"), help="new, empty output directory")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    build_dir = args.build_dir.resolve()
    output = args.output.resolve()
    idf_path_text = os.environ.get("IDF_PATH")
    tools_path_text = os.environ.get("IDF_TOOLS_PATH")
    if not idf_path_text or not tools_path_text:
        raise RuntimeError("Activate the ESP-IDF environment so IDF_PATH and IDF_TOOLS_PATH are set.")
    idf_path = Path(idf_path_text)
    tools_path = Path(tools_path_text)

    if output.exists():
        if any(output.iterdir()):
            raise RuntimeError(f"Output directory must be new or empty: {output}")
    else:
        output.mkdir(parents=True)

    link_map, libraries = linked_libraries(build_dir)

    notices: dict[str, dict[str, str]] = {}
    notices["LICENSE"] = copy_notice(root / "LICENSE", output / "LICENSE")
    notices["THIRD-PARTY-NOTICES.md"] = copy_notice(
        root / "THIRD-PARTY-NOTICES.md", output / "THIRD-PARTY-NOTICES.md"
    )

    components = linked_components(build_dir, libraries)
    idf_license_copied = False
    for component in components:
        component_root = Path(component["source_directory"]).resolve()
        if not idf_license_copied and (component_root == idf_path or idf_path in component_root.parents):
            notices["ESP-IDF-LICENSE"] = copy_notice(idf_path / "LICENSE", output / "ESP-IDF-LICENSE")
            idf_license_copied = True
        license_files = license_files_for_component(component)
        for source in license_files:
            name = notice_name(component, source)
            notices[name] = copy_notice(source, output / name, component)
        if license_files:
            continue
        for source, header in source_header_notices(component):
            name = notice_name(component, source) + ".notice"
            destination = output / name
            destination.write_text(header, encoding="utf-8", newline="\n")
            notices[name] = {
                "source": str(source),
                "sha256": sha256(destination),
                "component": component["component"],
                "library": component["library"],
            }

    if "c" in libraries:
        notices["Newlib-COPYING.txt"] = copy_notice(
            find_newlib_notice(tools_path), output / "Newlib-COPYING.txt"
        )

    manifest = {
        "format": 1,
        "project": "PSU-EXT firmware",
        "build_dir": str(build_dir),
        "link_map_sha256": sha256(link_map),
        "linked_static_libraries": libraries,
        "linked_components": [
            {key: value for key, value in component.items() if key != "source_files"}
            for component in components
        ],
        "dependencies_lock_sha256": sha256(require_file(root / "dependencies.lock")),
        "notices": notices,
    }
    (output / "MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Release notice bundle created: {output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
