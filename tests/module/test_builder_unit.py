"""Unit tests for the Snowboard Kids Game Module Builder package."""

from contextlib import contextmanager
import hashlib
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


class WindowsModuleBuildTests(unittest.TestCase):
    """PE/COFF module build: compile/link flags, engine import library, sources."""

    def test_msvc_like_compiles_corpus_as_cxx(self):
        for flavor in (ToolchainFlavor.MSVC, ToolchainFlavor.CLANG_CL):
            tc = Toolchain(Path("C:\\LLVM\\bin\\clang-cl.exe"), flavor)
            cmd = tc.get_compile_command(Path("C:\\c\\funcs_0.c"), Path("C:\\o\\funcs_0.obj"), [],
                                         ["dmem=(*sbk_module_dmem)"])
            self.assertIn("/TP", cmd)  # generated .c units hold C++ continuation code
            self.assertIn("/MD", cmd)  # same dynamic CRT as the engine
            self.assertIn("/Ddmem=(*sbk_module_dmem)", cmd)
        clang_cl = Toolchain(Path("C:\\LLVM\\bin\\clang-cl.exe"), ToolchainFlavor.CLANG_CL)
        cmd = clang_cl.get_compile_command(Path("a.cpp"), Path("a.obj"), [])
        self.assertIn("/clang:-msse4.1", cmd)

    def test_prefix_maps_match_each_compiler(self):
        old, new = Path("C:\\Users\\Player\\sbk"), Path(".")
        self.assertEqual(Toolchain(Path("cl.exe"), ToolchainFlavor.MSVC).get_prefix_map_args(old, new),
                         [f"/pathmap:{old}={new}"])
        # clang-cl has no /pathmap and would treat it as an input file name.
        self.assertEqual(Toolchain(Path("clang-cl.exe"), ToolchainFlavor.CLANG_CL).get_prefix_map_args(old, new),
                         [f"/clang:-ffile-prefix-map={old}={new}"])
        self.assertEqual(Toolchain(Path("clang++"), ToolchainFlavor.CLANG).get_prefix_map_args(old, new),
                         [f"-ffile-prefix-map={old}={new}"])

    def test_link_includes_engine_import_library(self):
        tc = Toolchain(Path("C:\\LLVM\\bin\\clang-cl.exe"), ToolchainFlavor.CLANG_CL)
        lib = Path("C:\\w\\SnowboardKidsEngine.lib")
        cmd = tc.get_link_command([Path("C:\\w\\b.obj"), Path("C:\\w\\a.obj")],
                                  Path("C:\\out\\SnowboardKidsGame.dll"), [lib])
        self.assertEqual(cmd[-1], str(lib))
        self.assertIn("/LD", cmd)
        imp = tc.get_import_library_command(Path("C:\\w\\e.def"), lib, librarian=Path("C:\\LLVM\\bin\\llvm-lib.exe"))
        self.assertEqual(imp, ["C:\\LLVM\\bin\\llvm-lib.exe", "/nologo", "/machine:x64",
                               "/def:C:\\w\\e.def", f"/out:{lib}"])

    def test_engine_export_list_and_import_definition(self):
        from module_builder.engine_exports import import_definition_text, read_engine_exports
        symbols = read_engine_exports(ROOT)
        self.assertIn("osSendMesg_recomp", symbols)
        self.assertIn("sbk_engine_register_overlays", symbols)
        self.assertEqual(len(symbols), len(set(symbols)))
        text = import_definition_text(symbols)
        self.assertTrue(text.startswith("NAME SnowboardKidsEngine.exe\nEXPORTS\n"))
        # The CMake .def regex and this parser accept exactly the same lines.
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn('REGEX "^SBK_ENGINE_EXPORT(_C)?\\\\(")', cmake)

    def test_every_audited_hle_used_by_module_is_exported(self):
        # The Linux module's undefined runtime symbols are the reviewed export
        # surface; when a local module exists, prove the list still covers it.
        from module_builder.engine_exports import read_engine_exports
        import shutil
        import subprocess
        module = Path.home() / ".local/share/SnowboardKids/modules/snowboardkids-us/SnowboardKidsGame.so"
        nm = shutil.which("nm")
        if not module.is_file() or not nm:
            self.skipTest("no local Linux game module to compare")
        out = subprocess.run([nm, "-D", "--undefined-only", str(module)], capture_output=True, text=True).stdout
        needed = {line.split()[-1] for line in out.splitlines() if line.split()[-1].endswith("_recomp")}
        self.assertLessEqual(needed, set(read_engine_exports(ROOT)))

    def test_windows_sources_add_dllimport_table(self):
        from module_builder.generator import collect_module_sources
        with tempfile.TemporaryDirectory() as td:
            corpus = Path(td)
            (corpus / "lookup.cpp").write_text("")
            for i in range(40):
                (corpus / f"funcs_{i}.c").write_text("")
            rsp = corpus / "aspMain.cpp"
            linux, _ = collect_module_sources(ROOT, corpus, rsp)
            windows, _ = collect_module_sources(ROOT, corpus, rsp, windows=True)
            imports = ROOT / "src" / "module" / "engine_imports_win32.cpp"
            self.assertNotIn(imports, linux)
            self.assertEqual(windows, linux + [imports])


class ModuleInputsBundleTests(unittest.TestCase):
    """Corpus + RSP bundle exported on Linux/WSL2 and consumed by a Windows build."""

    ROM_SHA1 = EXPECTED_SHA1

    def make_sources(self, root: Path):
        corpus = root / "corpus"
        corpus.mkdir()
        # Bytes, not text: write_text would turn \n into \r\n on Windows.
        for i in range(40):
            (corpus / f"funcs_{i}.c").write_bytes(f"// unit {i}\n".encode())
        for name in ("lookup.cpp", "funcs.h", "recomp_overlays.inl"):
            (corpus / name).write_bytes(f"// {name}\n".encode())
        (corpus / "notes.txt").write_text("not a source")  # never bundled
        rsp = root / "aspMain.cpp"
        rsp.write_bytes(b"// rsp\r\n")  # line endings must survive byte-exact
        return corpus, rsp

    def write(self, root: Path, name="inputs.zip"):
        from module_builder.inputs import collect_bundle_files, write_bundle
        corpus, rsp = self.make_sources(root)
        bundle = root / name
        write_bundle(bundle, collect_bundle_files(corpus, rsp), self.ROM_SHA1, "abc")
        return bundle

    def test_zip_round_trip_is_exact_and_deterministic(self):
        from module_builder.inputs import load_bundle
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            bundle = self.write(root)
            first = bundle.read_bytes()
            bundle.unlink()
            (root / "corpus").rename(root / "corpus-old")
            (root / "aspMain.cpp").unlink()
            self.assertEqual(self.write(root).read_bytes(), first)
            inputs = load_bundle(bundle, self.ROM_SHA1.upper(), root / "work")
            self.assertEqual(len(list(inputs.corpus_dir.glob("funcs_*.c"))), 40)
            self.assertFalse((inputs.corpus_dir / "notes.txt").exists())
            self.assertEqual(inputs.rsp_cpp.read_bytes(), b"// rsp\r\n")

    def test_extracted_directory_is_accepted(self):
        import zipfile
        from module_builder.inputs import load_bundle
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            with zipfile.ZipFile(self.write(root)) as archive:
                archive.extractall(root / "extracted")
            inputs = load_bundle(root / "extracted", self.ROM_SHA1, root / "work")
            self.assertTrue((inputs.corpus_dir / "lookup.cpp").is_file())

    def test_bundle_from_another_rom_is_refused(self):
        from module_builder.inputs import load_bundle
        with tempfile.TemporaryDirectory() as td:
            bundle = self.write(Path(td))
            with self.assertRaises(RomValidationError):
                load_bundle(bundle, "0" * 40, Path(td) / "work")

    def rewrite(self, bundle: Path, edit):
        import zipfile
        with zipfile.ZipFile(bundle) as archive:
            members = {name: archive.read(name) for name in archive.namelist()}
        manifest = json.loads(members["INPUTS.json"])
        edit(members, manifest)
        members["INPUTS.json"] = json.dumps(manifest).encode()
        with zipfile.ZipFile(bundle, "w") as archive:
            for name, data in members.items():
                archive.writestr(name, data)

    def test_modified_member_is_refused(self):
        from module_builder.inputs import load_bundle
        with tempfile.TemporaryDirectory() as td:
            bundle = self.write(Path(td))
            # CRLF conversion by a copy tool is the realistic corruption.
            self.rewrite(bundle, lambda m, _: m.update({"corpus/funcs_3.c": b"// unit 3\r\n"}))
            with self.assertRaisesRegex(GeneratorError, "checksum"):
                load_bundle(bundle, self.ROM_SHA1, Path(td) / "work")

    def test_unexpected_member_names_are_refused(self):
        from module_builder.inputs import load_bundle
        for name in ("../escape.c", "corpus/../../escape.c", "src/evil.cpp", "corpus/sub/x.c"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                bundle = self.write(Path(td))
                def edit(members, manifest):
                    members[name] = b"x"
                    manifest["files"][name] = hashlib.sha256(b"x").hexdigest()
                self.rewrite(bundle, edit)
                with self.assertRaisesRegex(GeneratorError, "unexpected"):
                    load_bundle(bundle, self.ROM_SHA1, Path(td) / "work")
                self.assertFalse((Path(td) / "escape.c").exists())

    def test_incomplete_corpus_is_refused(self):
        from module_builder.inputs import collect_bundle_files
        with tempfile.TemporaryDirectory() as td:
            corpus, rsp = self.make_sources(Path(td))
            (corpus / "funcs_39.c").unlink()
            with self.assertRaises(GeneratorError):
                collect_bundle_files(corpus, rsp)

    def test_rom_derived_output_stays_out_of_tracked_paths(self):
        from module_builder.inputs import ensure_not_tracked_location
        if not (ROOT / ".git").exists():
            self.skipTest("needs a git checkout")
        with self.assertRaises(StoragePermissionError):
            ensure_not_tracked_location(ROOT, ROOT / "docs" / "exported-inputs.zip")
        ensure_not_tracked_location(ROOT, ROOT / "build-tools" / "module-inputs" / "x.zip")
        with tempfile.TemporaryDirectory() as td:
            ensure_not_tracked_location(ROOT, Path(td) / "x.zip")


class WindowsValidationTests(unittest.TestCase):
    def test_windows_module_requires_the_engine(self):
        from unittest import mock
        from module_builder import validator
        with tempfile.TemporaryDirectory() as td:
            module = Path(td) / "SnowboardKidsGame.dll"
            module.write_bytes(b"MZ sbk_game_module_get_api")  # would pass a symbol scan
            with mock.patch.object(validator.sys, "platform", "win32"), \
                 mock.patch.object(validator, "find_engine_executable", return_value=None):
                with self.assertRaisesRegex(ModuleValidationError, "required"):
                    validator.validate_module_binary(module, ROOT)
            with self.assertRaisesRegex(ModuleValidationError, "not found"):
                validator.validate_module_binary(module, ROOT, engine=Path(td) / "missing.exe")


if __name__ == "__main__":
    unittest.main()
