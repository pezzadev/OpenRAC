# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""CI preparation isolation and a real metadata-only CMake restore: unittest discover -s tools."""

import copy
import contextlib
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

import ci_cache


ROOT = Path(__file__).resolve().parents[1]


class CachePolicy(unittest.TestCase):
    def test_native_compiler_caches_do_not_save_pr_private_snapshots(self):
        """Every native compiler cache can restore on PRs without evicting shared entries."""
        text = (ROOT / ".github/workflows/checks.yml").read_text()
        steps = re.split(r"(?m)^      - ", text)
        caches = [step for step in steps if "uses: hendrikmuhs/ccache-action@" in step]
        self.assertTrue(caches)
        for step in caches:
            self.assertIn("save: ${{ github.event_name != 'pull_request' }}", step)
        self.assertIn("CCACHE_COMPILERCHECK: content", text)

    def test_preparation_restore_and_save_use_identical_isolated_keys(self):
        """PRs restore the same preparation as branch builds, through a read-only action."""
        text = (ROOT / ".github/actions/port-build-cache/action.yml").read_text()
        steps = re.split(r"(?m)^    - ", text)
        write = next(step for step in steps if "uses: actions/cache@" in step)
        read = next(step for step in steps if "uses: actions/cache/restore@" in step)
        self.assertIn("github.event_name != 'pull_request'", write)
        self.assertIn("github.event_name == 'pull_request'", read)
        self.assertEqual(write.split("      with:\n")[1], read.split("      with:\n")[1])
        self.assertIn("hashFiles('games/**',", read)
        self.assertIn("'tools/openrac_common/**'", read)
        self.assertIn("!port/build/${{ inputs.preset }}/_deps/openrac_compressonator-src", read)

    def test_rust_cache_does_not_save_pr_private_snapshots(self):
        """Large Rust snapshots must not compete with shared native build caches on every PR."""
        text = (ROOT / ".github/actions/launcher-toolchain/action.yml").read_text()
        self.assertIn("save-if: ${{ github.event_name != 'pull_request' }}", text)


@unittest.skipUnless(shutil.which("ccache") and shutil.which("clang"), "ccache and Clang required")
@unittest.skipIf(os.name == "nt", "The compiler timestamp fixture uses a POSIX executable")
class CompilerRestore(unittest.TestCase):
    def test_compiler_reinstallation_reuses_objects_but_replacement_does_not(self):
        """Content identity hits across mtime changes and misses when compiler bytes change."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            compiler, source, obj = root / "compiler", root / "source.c", root / "source.o"
            compiler.write_text('#!/bin/sh\nexec "' + shutil.which("clang") + '" "$@"\n')
            compiler.chmod(0o755)
            source.write_text("int result(void) { return 42; }\n")
            env = {key: value for key, value in os.environ.items() if not key.startswith("CCACHE_")}
            env.update(CCACHE_DIR=str(root / "cache"), CCACHE_CONFIGPATH=str(root / "ccache.conf"),
                       CCACHE_COMPILERCHECK="content", CCACHE_LOGFILE=str(root / "ccache.log"))
            command = ["ccache", str(compiler), "-c", str(source), "-o", str(obj)]
            def compile_result():
                """Return this compilation's cache result without cumulative counters."""
                (root / "ccache.log").unlink(missing_ok=True)
                subprocess.run(command, env=env, check=True, capture_output=True)
                return (root / "ccache.log").read_text()
            self.assertIn("Result: cache_miss", compile_result())
            original = obj.read_bytes()
            changed_time = compiler.stat().st_mtime_ns + 10_000_000_000
            os.utime(compiler, ns=(changed_time, changed_time))
            obj.unlink()
            self.assertIn("Result: direct_cache_hit", compile_result())
            self.assertEqual(original, obj.read_bytes())
            compiler.write_text(compiler.read_text() + "# Compiler replacement\n")
            self.assertIn("Result: cache_miss", compile_result())


class Identity(unittest.TestCase):
    def test_environment_changes_invalidate_configure_results(self):
        identity = {"workspace": "/work", "tools": [{"path": "/clang", "sha256": "a"}],
                    "python": {"path": "/python", "version": "3.12"},
                    "environment": {"ImageVersion": "1"}, "packages": ["sdk=1"]}
        original = ci_cache.fingerprint(identity)
        self.assertEqual(original, ci_cache.fingerprint(dict(reversed(list(identity.items())))))
        for field, changed in (("workspace", "/other"), ("tools", [{"path": "/clang", "sha256": "b"}]),
                               ("python", {"path": "/python", "version": "3.13"}),
                               ("environment", {"ImageVersion": "2"}), ("packages", ["sdk=2"])):
            variant = copy.deepcopy(identity)
            variant[field] = changed
            self.assertNotEqual(original, ci_cache.fingerprint(variant), field)

    def test_compiler_replacement_changes_identity_even_with_same_version(self):
        with tempfile.TemporaryDirectory() as temporary:
            program = Path(temporary) / "compiler"
            program.write_bytes(b"first compiler")
            with mock.patch("ci_cache.shutil.which", return_value=str(program)), \
                    mock.patch("ci_cache.output", return_value="same advertised version"):
                first = ci_cache.tool_identity("compiler")
                # Each job is a new process; simulate replacing a tool before the next job.
                ci_cache.file_digest.cache_clear()
                program.write_bytes(b"changed compiler")
                self.assertNotEqual(first, ci_cache.tool_identity("compiler"))
            ci_cache.file_digest.cache_clear()

    def test_missing_tool_fails_before_restore(self):
        with mock.patch("ci_cache.shutil.which", return_value=None):
            with self.assertRaisesRegex(RuntimeError, "not found"):
                ci_cache.tool_identity("missing")

    def test_missing_identity_leaves_cache_disabled(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary) / "outputs"
            with mock.patch.dict(os.environ, {"GITHUB_OUTPUT": str(out)}), \
                    mock.patch("ci_cache.installed_identity", side_effect=RuntimeError("no SDK identity")), \
                    contextlib.redirect_stdout(io.StringIO()) as log:
                ci_cache.main()
            self.assertIn("Skipping CI preparation cache", log.getvalue())
            self.assertFalse(out.exists())

    def test_runner_changes_affect_collected_identity(self):
        with mock.patch("ci_cache.tool_identity", return_value={"version": "same compiler"}), \
                mock.patch("ci_cache.system_packages", return_value=["sdk=1"]), \
                mock.patch("ci_cache.Path.read_text", return_value="flags\t: sse2 avx\n"), \
                mock.patch.dict(os.environ, {"OPENRAC_CLANG": "/tools/clang", "ImageVersion": "one"}):
            first = ci_cache.fingerprint(ci_cache.installed_identity())
            with mock.patch.dict(os.environ, {"ImageVersion": "two"}):
                self.assertNotEqual(first, ci_cache.fingerprint(ci_cache.installed_identity()))
            with mock.patch("ci_cache.Path.cwd", return_value=Path("/another-workspace")):
                self.assertNotEqual(first, ci_cache.fingerprint(ci_cache.installed_identity()))


@unittest.skipUnless(shutil.which("cmake") and shutil.which("ninja") and shutil.which("clang"),
                     "CMake, Ninja and Clang required")
@unittest.skipIf(os.name == "nt" and not os.environ.get("INCLUDE"),
                 "Configure the Windows compiler environment before running the CMake fixture")
class ConfigureRestore(unittest.TestCase):
    def test_metadata_reuses_probes_but_rebuilds_changed_sources(self):
        """Restore no objects/Ninja files; retain a probe and build/test the updated program."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, build, saved = root / "source", root / "build", root / "saved"
            source.mkdir()
            (source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.24)
project(CacheRestore C)
include(CheckCSourceCompiles)
check_c_source_compiles("int main(void) { return 0; }" WORKING_COMPILER)
if(NOT WORKING_COMPILER)
  message(FATAL_ERROR "Compiler probe failed")
endif()
enable_testing()
add_executable(probe main.c)
add_test(NAME actual_program COMMAND probe)
''')
            (source / "main.c").write_text("int main(void) { return 1; }\n")
            configure = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                         "-DCMAKE_C_COMPILER=" + shutil.which("clang")]
            run = lambda command: subprocess.run(command, capture_output=True, text=True, check=True)
            first = run(configure)
            self.assertIn("Performing Test WORKING_COMPILER", first.stdout)
            run(["cmake", "--build", str(build)])
            saved.mkdir()
            shutil.copy2(build / "CMakeCache.txt", saved / "CMakeCache.txt")
            # The action restores only CMake's versioned compiler metadata, not target directories.
            for directory in (build / "CMakeFiles").glob("[0-9]*"):
                shutil.copytree(directory, saved / "CMakeFiles" / directory.name,
                                ignore=shutil.ignore_patterns("*.o", "*.obj", "*.pdb"))
            self.assertEqual(build.resolve().parent, root.resolve())
            shutil.rmtree(build)
            shutil.copytree(saved, build)
            self.assertFalse((build / "build.ninja").exists())
            self.assertFalse(list(build.glob("probe*")))
            self.assertFalse(list(build.rglob("*.o")))
            self.assertFalse(list(build.rglob("*.obj")))
            (source / "main.c").write_text("int main(void) { return 0; }\n")
            second = run(configure)
            self.assertNotIn("Performing Test WORKING_COMPILER", second.stdout)
            rebuilt = run(["cmake", "--build", str(build)])
            self.assertIn("main.c", rebuilt.stdout)
            run(["ctest", "--test-dir", str(build), "--output-on-failure", "--no-tests=error"])


if __name__ == "__main__":
    unittest.main()
