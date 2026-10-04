"""Snowboard Kids Game Module Builder package."""

from .errors import (
    BuilderError,
    BuilderExitCode,
    CompileError,
    GeneratorError,
    LinkError,
    MissingCompilerError,
    ModuleValidationError,
    RomValidationError,
    StoragePermissionError,
)
from .rom import (
    EXPECTED_SHA1,
    EXPECTED_SHA256,
    EXPECTED_ROM_HASH_HEX,
    EXPECTED_ROM_HASH_U64,
    detect_and_normalize_rom,
    validate_rom,
)
from .service import (
    BuilderConfig,
    BuilderResult,
    ModuleBuilderService,
    default_module_filename,
    default_user_module_dir,
)
from .toolchain import (
    Toolchain,
    ToolchainFlavor,
    discover_toolchain,
)

__all__ = [
    "BuilderConfig",
    "BuilderError",
    "BuilderExitCode",
    "BuilderResult",
    "CompileError",
    "GeneratorError",
    "LinkError",
    "EXPECTED_SHA1",
    "EXPECTED_SHA256",
    "MissingCompilerError",
    "ModuleBuilderService",
    "ModuleValidationError",
    "RomValidationError",
    "StoragePermissionError",
    "Toolchain",
    "ToolchainFlavor",
    "default_module_filename",
    "default_user_module_dir",
    "detect_and_normalize_rom",
    "discover_toolchain",
    "validate_rom",
]
