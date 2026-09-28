"""Non-proprietary module manifest generation (MODULE-INFO.json)."""

from datetime import datetime, timezone
import json
from pathlib import Path
from typing import Any, Dict


MANIFEST_SCHEMA_VERSION = 1
GENERATOR_VERSION = "1.0.0"
ENGINE_COMPATIBILITY_SPEC = ">=1.0.0"


def generate_manifest_data(
    extracted_metadata: Dict[str, Any],
    rom_sha1: str,
) -> Dict[str, Any]:
    """
    Constructs clean, non-proprietary metadata for MODULE-INFO.json.
    Excludes any personal paths, usernames, workspace paths, or ROM bytes.
    """
    manifest = {
        "manifest_schema_version": MANIFEST_SCHEMA_VERSION,
        "generator_version": GENERATOR_VERSION,
        "engine_compatibility": ENGINE_COMPATIBILITY_SPEC,
        "created_at_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "game_id": extracted_metadata.get("game_id", "snowboardkids.n64.us"),
        "internal_name": extracted_metadata.get("internal_name", "SNOWBOARD KIDS"),
        "abi_version": extracted_metadata.get("abi_version", 1),
        "rom_hash": extracted_metadata.get("rom_hash", "0xF384619787B78D4B"),
        "rom_sha1": rom_sha1.lower(),
        "corpus_digest": extracted_metadata.get("corpus_digest", "0x0"),
        "function_count": extracted_metadata.get("function_count", 1981),
        "hle_count": extracted_metadata.get("hle_count", 56),
        "entrypoint_address": extracted_metadata.get("entrypoint_address", "0x80000400"),
        "continuation_count": extracted_metadata.get("continuation_count", 1981),
    }
    return manifest


def write_manifest_file(manifest_path: Path, manifest_data: Dict[str, Any]) -> None:
    """Writes the manifest data formatted as indented JSON."""
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest_data, indent=2) + "\n", encoding="utf-8")
