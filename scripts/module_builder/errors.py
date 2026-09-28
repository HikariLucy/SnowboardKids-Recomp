"""Builder exceptions and exit codes for Snowboard Kids game module compilation."""

class BuilderExitCode:
    SUCCESS = 0
    WRONG_ROM = 1
    MISSING_COMPILER = 2
    GENERATOR_FAILURE = 3
    COMPILE_FAILURE = 4
    LINK_FAILURE = 5
    VALIDATION_FAILURE = 6
    IO_PERMISSION_ERROR = 7
    USER_CANCELLED = 8


class BuilderError(Exception):
    """Base exception for module builder operations."""
    def __init__(self, message: str, exit_code: int = 1):
        super().__init__(message)
        self.message = message
        self.exit_code = exit_code


class RomValidationError(BuilderError):
    def __init__(self, message: str):
        super().__init__(message, exit_code=BuilderExitCode.WRONG_ROM)


class MissingCompilerError(BuilderError):
    def __init__(self, message: str):
        super().__init__(message, exit_code=BuilderExitCode.MISSING_COMPILER)


class GeneratorError(BuilderError):
    def __init__(self, message: str):
        super().__init__(message, exit_code=BuilderExitCode.GENERATOR_FAILURE)


class CompileError(BuilderError):
    def __init__(self, message: str):
        super().__init__(message, exit_code=BuilderExitCode.COMPILE_FAILURE)


class LinkError(BuilderError):
    def __init__(self, message: str):
        super().__init__(message, exit_code=BuilderExitCode.LINK_FAILURE)


class ModuleValidationError(BuilderError):
    def __init__(self, message: str):
        super().__init__(message, exit_code=BuilderExitCode.VALIDATION_FAILURE)


class StoragePermissionError(BuilderError):
    def __init__(self, message: str):
        super().__init__(message, exit_code=BuilderExitCode.IO_PERMISSION_ERROR)
