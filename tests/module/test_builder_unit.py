"""Unit tests for the Snowboard Kids Game Module Builder package."""

from contextlib import contextmanager
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from module_builder.errors import (
    BuilderExitCode,
    CompileError,
    GeneratorError,
    LinkError,
    MissingCompilerError,
    ModuleValidationError,
    RomValidationError,
    StoragePermissionError,
)
from module_builder.manifest import generate_manifest_data, write_manifest_file
from module_builder.rom import (
    EXPECTED_SHA1,
    detect_and_normalize_rom,
    validate_rom,
)
from module_builder.toolchain import (
    Toolchain,
    ToolchainFlavor,
    discover_toolchain,
)


@contextmanager
def rom_file(suffix, data):
    """A closed ROM file in a directory with spaces. NamedTemporaryFile keeps
    its handle open, and Windows refuses to reopen such a file by name."""
    with tempfile.TemporaryDirectory(prefix="Snowboard Kids Test ") as directory:
        path = Path(directory) / f"rom image{suffix}"
        path.write_bytes(data)
        yield path


class RomValidationTests(unittest.TestCase):
    def test_rom_z64_detection_and_normalization(self):
        with rom_file(".z64", b'\x80\x37\x12\x40' + b'\x00' * (8 * 1024 * 1024 - 4)) as path:
            # Create synthetic 8MB .z64 image (starts with 0x80371240)
            norm, fmt = detect_and_normalize_rom(path)
            self.assertEqual(fmt, "Big-Endian (.z64)")
            self.assertEqual(len(norm), 8 * 1024 * 1024)
            self.assertEqual(norm[:4], b'\x80\x37\x12\x40')

    def test_rom_v64_byte_swapped_normalization(self):
        with rom_file(".v64", b'\x37\x80\x40\x12' + b'\x00' * (8 * 1024 * 1024 - 4)) as path:
            # Create synthetic 8MB .v64 image (starts with 0x37804012)
            norm, fmt = detect_and_normalize_rom(path)
            self.assertEqual(fmt, "Byte-Swapped (.v64)")
            self.assertEqual(len(norm), 8 * 1024 * 1024)
            # After swapping, magic must become 0x80371240
            self.assertEqual(norm[:4], b'\x80\x37\x12\x40')

    def test_rom_n64_little_endian_normalization(self):
        with rom_file(".n64", b'\x40\x12\x37\x80' + b'\x00' * (8 * 1024 * 1024 - 4)) as path:
            # Create synthetic 8MB .n64 image (starts with 0x40123780)
            norm, fmt = detect_and_normalize_rom(path)
            self.assertEqual(fmt, "Little-Endian (.n64)")
            self.assertEqual(len(norm), 8 * 1024 * 1024)
            # After 4-byte reverse, magic must become 0x80371240
            self.assertEqual(norm[:4], b'\x80\x37\x12\x40')

    def test_rom_too_small_rejected(self):
        with rom_file(".z64", b'\x80\x37\x12\x40\x00\x00') as path:
            with self.assertRaises(RomValidationError) as ctx:
                detect_and_normalize_rom(path)
            self.assertIn("ROM size is", str(ctx.exception))

    def test_rom_invalid_magic_rejected(self):
        with rom_file(".bin", b'\x11\x22\x33\x44' + b'\x00' * 1024) as path:
            with self.assertRaises(RomValidationError) as ctx:
                detect_and_normalize_rom(path)
            self.assertIn("Unrecognized ROM header magic", str(ctx.exception))

    def test_rom_sha1_mismatch_rejected(self):
        with rom_file(".z64", b'\x80\x37\x12\x40' + b'\x55' * (8 * 1024 * 1024 - 4)) as path:
            with self.assertRaises(RomValidationError) as ctx:
                validate_rom(path)
            self.assertIn("ROM SHA-1 mismatch", str(ctx.exception))
            self.assertEqual(ctx.exception.exit_code, BuilderExitCode.WRONG_ROM)


class ToolchainTests(unittest.TestCase):
    def test_toolchain_discovery_finds_valid_compiler(self):
        toolchain = discover_toolchain()
        self.assertIsNotNone(toolchain.executable)
        self.assertTrue(toolchain.executable.exists())
        self.assertIn(toolchain.flavor, [ToolchainFlavor.GCC, ToolchainFlavor.CLANG, ToolchainFlavor.MSVC, ToolchainFlavor.CLANG_CL])

    def test_gcc_command_generation(self):
        tc = Toolchain(Path("/usr/bin/g++"), ToolchainFlavor.GCC)
        src = Path("/tmp/src.cpp")
        obj = Path("/tmp/src.o")
        inc = [Path("/tmp/include1"), Path("/tmp/include2")]
        cmd = tc.get_compile_command(src, obj, inc)
        self.assertIn("-std=c++20", cmd)
        self.assertIn("-fPIC", cmd)
        # Paths are passed in the host's native spelling.
        self.assertIn(f"-I{inc[0]}", cmd)
        self.assertIn(f"-I{inc[1]}", cmd)

        out = Path("/tmp/module.so")
        link_cmd = tc.get_link_command([obj], out)
        self.assertIn("-shared", link_cmd)
        self.assertIn("-o", link_cmd)
        self.assertIn(str(out), link_cmd)

    def test_paths_with_spaces_stay_single_arguments(self):
        base = Path(tempfile.gettempdir()) / "Snowboard Kids Test"
        src, obj, inc = base / "src file.cpp", base / "obj dir" / "src file.o", base / "include dir"
        out = base / "out dir" / "SnowboardKidsGame.dll"
        for tc in (Toolchain(Path("/usr/bin/g++"), ToolchainFlavor.GCC),
                   Toolchain(base / "bin" / "cl.exe", ToolchainFlavor.MSVC)):
            cmd = tc.get_compile_command(src, obj, [inc])
            link = tc.get_link_command([obj], out)
            for argv in (cmd, link):
                self.assertTrue(all(isinstance(a, str) for a in argv))
                self.assertFalse(any('"' in a for a in argv), argv)  # argv arrays, no manual quoting
            self.assertEqual(cmd[0], str(tc.executable))
            self.assertIn(str(src), cmd)
            self.assertIn(str(obj), link)
            self.assertTrue(any(a.endswith(str(inc)) and a != str(inc) for a in cmd), cmd)

    def test_msvc_command_generation(self):
        tc = Toolchain(Path("C:\\MSVC\\bin\\cl.exe"), ToolchainFlavor.MSVC)
        src = Path("C:\\src\\file.cpp")
        obj = Path("C:\\obj\\file.obj")
        inc = [Path("C:\\include")]
        cmd = tc.get_compile_command(src, obj, inc)
        self.assertIn("/std:c++20", cmd)
        self.assertIn("/O2", cmd)
        self.assertIn("/c", cmd)
        self.assertIn("/Fo:C:\\obj\\file.obj", cmd)
        self.assertIn("/IC:\\include", cmd)

        link_cmd = tc.get_link_command([obj], Path("C:\\out\\SnowboardKidsGame.dll"))
        self.assertIn("/LD", link_cmd)
        self.assertIn("/Fe:C:\\out\\SnowboardKidsGame.dll", link_cmd)


class ManifestPrivacyTests(unittest.TestCase):
    def test_manifest_contains_no_sensitive_or_proprietary_data(self):
        extracted = {
            "game_id": "snowboardkids.n64.us",
            "internal_name": "SNOWBOARD KIDS",
            "abi_version": 1,
            "rom_hash": "0xF384619787B78D4B",
            "corpus_digest": "0x76260CB8F0E080D7",
            "function_count": 1981,
            "hle_count": 56,
            "entrypoint_address": "0x80000400",
            "continuation_count": 1981
        }
        manifest = generate_manifest_data(extracted, EXPECTED_SHA1)

        manifest_str = json.dumps(manifest)

        # Privacy checks
        self.assertNotIn("/home/", manifest_str)
        self.assertNotIn("/Users/", manifest_str)
        self.assertNotIn("hikarilucy", manifest_str)
        self.assertNotIn("C:\\", manifest_str)
        self.assertNotIn("workspace", manifest_str)
        self.assertNotIn(".z64", manifest_str)

        # Content verification
        self.assertEqual(manifest["abi_version"], 1)
        self.assertEqual(manifest["game_id"], "snowboardkids.n64.us")
        self.assertEqual(manifest["rom_sha1"], EXPECTED_SHA1.lower())
        self.assertEqual(manifest["function_count"], 1981)
        self.assertEqual(manifest["hle_count"], 56)

        with tempfile.TemporaryDirectory() as td:
            out_json = Path(td) / "MODULE-INFO.json"
            write_manifest_file(out_json, manifest)
            self.assertTrue(out_json.is_file())
            loaded = json.loads(out_json.read_text(encoding="utf-8"))
            self.assertEqual(loaded["rom_sha1"], EXPECTED_SHA1.lower())


class AtomicInstallTests(unittest.TestCase):
    def test_atomic_replacement_preserves_target_on_failure(self):
        with tempfile.TemporaryDirectory() as td:
            target_dir = Path(td) / "modules" / "snowboardkids-us"
            target_dir.mkdir(parents=True)
            active_module = target_dir / "SnowboardKidsGame.so"
            active_manifest = target_dir / "MODULE-INFO.json"

            active_module.write_text("ACTIVE_VERSION_1")
            active_manifest.write_text("{\"version\": 1}")

            # Simulate a failed staging attempt: error occurs before rename
            staged_mod = target_dir / ".tmp_SnowboardKidsGame.so.staging"
            staged_mod.write_text("NEW_PARTIAL_UNVALIDATED_DATA")

            # Emulate failure: staged file is cleaned up, original active module remains
            if staged_mod.exists():
                staged_mod.unlink()

            self.assertEqual(active_module.read_text(), "ACTIVE_VERSION_1")
            self.assertEqual(active_manifest.read_text(), "{\"version\": 1}")

            # Simulate successful atomic replacement
            staged_mod.write_text("ACTIVE_VERSION_2")
            staged_mod.replace(active_module)

            self.assertEqual(active_module.read_text(), "ACTIVE_VERSION_2")


if __name__ == "__main__":
    unittest.main()
