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

"""Host regression tests for the actual SCPI pattern matcher and registry C code.

Windows: --compiler accepts MSVC cl.exe or a host-capable clang.exe.
No ESP-IDF runtime, connected board, or production NVS is used.
"""
import argparse
import concurrent.futures
import ctypes as c
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCPI = ROOT / "components/scpi_handler"
VISITOR = c.CFUNCTYPE(c.c_bool, c.c_char_p, c.c_void_p)
CALLBACK = c.CFUNCTYPE(None, c.c_char_p, c.c_void_p, c.c_void_p)


class Command(c.Structure):
    _fields_ = [("keyword", c.c_char_p), ("callback", CALLBACK), ("context", c.c_void_p)]


def build_library(directory, compiler):
    stub = directory / "include"
    (stub / "freertos").mkdir(parents=True)
    headers = {
        "stddef.h": "typedef __SIZE_TYPE__ size_t;\n#define NULL ((void *)0)\n",
        "stdint.h": "typedef unsigned char uint8_t; typedef unsigned int uint32_t;\n#define UINT32_C(x) x##U\n",
        "stdbool.h": "#define bool _Bool\n#define true 1\n#define false 0\n",
        "string.h": "#include <stddef.h>\nvoid *memcpy(void *, const void *, size_t); void *memset(void *, int, size_t); size_t strlen(const char *); size_t strnlen(const char *, size_t); int strcmp(const char *, const char *); char *strchr(const char *, int); char *strpbrk(const char *, const char *);\n",
        "esp_err.h": "typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_STATE 0x103\n",
        "freertos/FreeRTOS.h": "",
        "freertos/portmacro.h": "typedef unsigned portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED 0\n#define portENTER_CRITICAL(p) while (__atomic_exchange_n(p, 1, __ATOMIC_ACQUIRE)) {}\n#define portEXIT_CRITICAL(p) __atomic_store_n(p, 0, __ATOMIC_RELEASE)\n",
    }
    msvc = Path(compiler).name.lower() == "cl.exe"
    if msvc:
        headers["stddef.h"] = "typedef unsigned long long size_t;\n#define NULL ((void *)0)\n"
        headers["freertos/portmacro.h"] = ("typedef long portMUX_TYPE;\n"
            "long _InterlockedExchange(long volatile *, long);\n#pragma intrinsic(_InterlockedExchange)\n"
            "#define portMUX_INITIALIZER_UNLOCKED 0\n"
            "#define portENTER_CRITICAL(p) while (_InterlockedExchange(p, 1)) {}\n"
            "#define portEXIT_CRITICAL(p) _InterlockedExchange(p, 0)\n")
    for name, text in headers.items():
        (stub / name).write_text(text)
    runtime = directory / "runtime.c"
    runtime.write_text("""#include <string.h>
void *memcpy(void *d, const void *s, size_t n) { unsigned char *a=d; const unsigned char *b=s; while(n--) *a++=*b++; return d; }
void *memset(void *d, int v, size_t n) { unsigned char *a=d; while(n--) *a++=(unsigned char)v; return d; }
size_t strlen(const char *s) { size_t n=0; while(s[n]) ++n; return n; }
size_t strnlen(const char *s, size_t m) { size_t n=0; while(n<m && s[n]) ++n; return n; }
int strcmp(const char *a, const char *b) { while(*a && *a==*b) { ++a; ++b; } return (unsigned char)*a-(unsigned char)*b; }
char *strchr(const char *s, int ch) { do { if(*s==ch) return (char *)s; } while(*s++); return NULL; }
char *strpbrk(const char *s, const char *set) { for(;*s;++s) if(strchr(set,*s)) return (char *)s; return NULL; }
""")
    exports = ["scpi_handler_pattern_valid", "scpi_handler_pattern_match", "scpi_handler_pattern_visit",
               "scpi_handler_register_commands", "scpi_handler_register_patterns",
               "scpi_handler_dispatch_registered_command"]
    sources = [SCPI / "scpi_handler_pattern.c", SCPI / "scpi_handler_registry.c", runtime]
    includes = ["-I" + str(stub), "-I" + str(SCPI), "-I" + str(SCPI / "include")]
    library = directory / ("scpi.dll" if os.name == "nt" else "scpi.so")
    if os.name == "nt":
        objects = []
        for source in sources:
            obj = directory / (source.stem + ".obj")
            if msvc:
                subprocess.run([compiler, "/nologo", "/std:c11", "/O2", "/Oi-", "/W4", "/WX",
                                "/GS-", "/Zl", "/X", *["/I" + i[2:] for i in includes],
                                "/c", str(source), "/Fo" + str(obj)], check=True, capture_output=True)
                objects.append(str(obj))
                continue
            subprocess.run([compiler, "--target=x86_64-pc-windows-msvc", "-std=c11", "-O2",
                            "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-builtin",
                            "-fno-stack-protector", "-nostdinc", *includes,
                            "-c", str(source), "-o", str(obj)], check=True, capture_output=True)
            objects.append(str(obj))
        linker = [str(Path(compiler).with_name("link.exe")), "/nologo", "/nodefaultlib"] if msvc else [str(Path(compiler).with_name("lld.exe")), "-flavor", "link"]
        subprocess.run([*linker, "/dll", "/noentry", "/out:" + str(library),
                        *["/export:" + name for name in exports], *objects], check=True, capture_output=True)
    else:
        subprocess.run([compiler, "-shared", "-fPIC", "-pthread", "-std=c11", "-O2",
                        "-Wall", "-Wextra", "-Werror", "-fno-builtin", *includes,
                        *map(str, sources), "-o", str(library)], check=True, capture_output=True)
    dll = c.CDLL(str(library))
    dll.scpi_handler_pattern_valid.argtypes = [c.c_char_p]
    dll.scpi_handler_pattern_valid.restype = c.c_bool
    dll.scpi_handler_pattern_match.argtypes = [c.c_char_p, c.c_char_p]
    dll.scpi_handler_pattern_match.restype = c.c_bool
    dll.scpi_handler_pattern_visit.argtypes = [c.c_char_p, VISITOR, c.c_void_p]
    dll.scpi_handler_pattern_visit.restype = c.c_bool
    for name in ("scpi_handler_register_commands", "scpi_handler_register_patterns"):
        getattr(dll, name).argtypes = [c.POINTER(Command), c.c_size_t]
        getattr(dll, name).restype = c.c_int
    dll.scpi_handler_dispatch_registered_command.argtypes = [c.c_char_p, c.c_char_p, c.c_void_p]
    dll.scpi_handler_dispatch_registered_command.restype = c.c_bool
    return dll


class PatternTests(unittest.TestCase):
    def expand(self, pattern):
        result = []
        visitor = VISITOR(lambda word, _: result.append(word.decode()) is None)
        self.assertTrue(DLL.scpi_handler_pattern_visit(pattern.encode(), visitor, None))
        return result

    def test_original_aliases_and_callback_mapping(self):
        fixture = json.loads((SCPI / "test/command_aliases.json").read_text())
        builtins = []
        total = 0
        for filename, callbacks in fixture.items():
            entries = re.findall(r'\{"([^"\n]+)",\s*(\w+)(?:,\s*(?:&\w+|NULL))?\}',
                                 (ROOT / filename).read_text())
            self.assertEqual(len(callbacks), len(entries), filename)
            self.assertEqual(set(callbacks), {callback for _, callback in entries})
            for pattern, callback in entries:
                with self.subTest(file=filename, callback=callback):
                    self.assertTrue(DLL.scpi_handler_pattern_valid(pattern.encode()), pattern)
                    words = self.expand(pattern)
                    self.assertEqual(len(words), len(set(words)), pattern)
                    self.assertEqual(set(callbacks[callback]), set(words), pattern)
                    total += len(words)
                    for word in words:
                        self.assertTrue(DLL.scpi_handler_pattern_match(pattern.encode(), word.encode()))
                        self.assertFalse(DLL.scpi_handler_pattern_match(pattern.encode(), (word + "X").encode()))
            if "components/scpi_handler/" in filename:
                builtins.extend(entries)
        for pattern, callback in builtins:
            for word in self.expand(pattern):
                matches = [(p, cb) for p, cb in builtins
                           if DLL.scpi_handler_pattern_match(p.encode(), word.encode())]
                self.assertEqual([(pattern, callback)], matches, word)
        self.assertEqual(381, total)

    def test_grammar_and_limits(self):
        invalid = [None, b"", b"[", b"(A)", b"(A|)", b"(|A)", b"A|B", b"[A|B]",
                   b"[A)", b"(A|B]", b"a", b"A B", b"A?B", b"[A]", b"[]",
                   b"A[[[[[B]]]]]", b"A[B][C][D][E][F][G][H][I][J]", b"A" * 128]
        for pattern in invalid:
            self.assertFalse(DLL.scpi_handler_pattern_valid(pattern), pattern)
        for pattern in [b"A[[[[B]]]]", b"A[B][C][D][E][F][G][H][I]", b"(A|B)[?]", b"A" * 127]:
            self.assertTrue(DLL.scpi_handler_pattern_valid(pattern), pattern)
        self.assertEqual(256, len(self.expand("A[B][C][D][E][F][G][H][I]")))
        pattern = "X(A[B]|C(D|E))Y"
        self.assertTrue(DLL.scpi_handler_pattern_valid(pattern.encode()))
        self.assertEqual({"XAY", "XABY", "XCDY", "XCEY"}, set(self.expand(pattern)))
        for word in ["XY", "XAB", "XACY", "XCDYY"]:
            self.assertFalse(DLL.scpi_handler_pattern_match(pattern.encode(), word.encode()))

    def test_registry_atomicity_concurrency_and_capacity(self):
        calls = []
        callback = CALLBACK(lambda argument, writer, context: calls.append((argument, context)))
        keepalive = []
        def register(patterns, patterned=True, context=42):
            table = (Command * len(patterns))(*[Command(p.encode(), callback, context) for p in patterns])
            keepalive.append(table)
            fn = DLL.scpi_handler_register_patterns if patterned else DLL.scpi_handler_register_commands
            return fn(table, len(table))
        dispatch = DLL.scpi_handler_dispatch_registered_command
        self.assertEqual(0x102, DLL.scpi_handler_register_patterns(None, 1))
        self.assertEqual(0x102, DLL.scpi_handler_register_patterns(None, 0))
        self.assertEqual(0x101, register(["HOST:X?"] * 33))
        self.assertEqual(0, register(["HOST:(PAT|PATTERN)?"]))
        self.assertTrue(dispatch(b"HOST:PAT?", b"Mixed argument", None))
        self.assertEqual([(b"Mixed argument", 42)], calls)
        self.assertEqual(0x103, register(["HOST:PAT?"], False))
        self.assertEqual(0x103, register(["HOST:(PAT|OTHER)?"]))
        self.assertEqual(0x102, register(["HOST:(NEW|NEWER)?", "HOST:NEW?"]))
        self.assertEqual(0x102, register(["HOST:(BAD|GOOD)?"], False))
        self.assertEqual(0x103, register(["HOST:NOTPUBLISHED?", "HOST:PAT?"]))
        self.assertFalse(dispatch(b"HOST:NOTPUBLISHED?", None, None))
        self.assertEqual(0x102, register(["HOST:ATOMIC?", "(BAD|)"]))
        self.assertFalse(dispatch(b"HOST:ATOMIC?", None, None))
        self.assertEqual(0, register(["HOST:LITERAL?"], False))
        self.assertEqual(0x103, register(["HOST:(LITERAL|DIFFERENT)?"]))
        # Two threads race to publish overlapping aliases; exactly one wins.
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            futures = [pool.submit(register, ["HOST:(RACE|RACING)?"]) for _ in range(2)]
            readers = [pool.submit(dispatch, b"HOST:PATTERN?", None, None) for _ in range(20)]
            self.assertEqual([0, 0x103], sorted(f.result() for f in futures))
            self.assertTrue(all(f.result() for f in readers))
        # Three descriptors are published; failed registrations consume none.
        for i in range(29):
            self.assertEqual(0, register([f"HOST:FILL{i}?"]))
        self.assertEqual(0x101, register(["HOST:FULL?"]))
        self.assertEqual(0x103, register(["HOST:FILL0?"], False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=shutil.which("cc"))
    args = parser.parse_args()
    if not args.compiler:
        parser.error("Supply --compiler with a C compiler path")
    with tempfile.TemporaryDirectory(prefix="scpi-host-") as temporary:
        try:
            DLL = build_library(Path(temporary), args.compiler)
            result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(PatternTests))
            # Windows keeps loaded DLLs locked until explicitly unloaded.
            if os.name == "nt":
                handle = DLL._handle
                del DLL
                c.windll.kernel32.FreeLibrary.argtypes = [c.c_void_p]
                c.windll.kernel32.FreeLibrary(handle)
            raise SystemExit(0 if result.wasSuccessful() else 1)
        except subprocess.CalledProcessError as error:
            print(error.stdout.decode(errors="replace"))
            print(error.stderr.decode(errors="replace"))
            raise
