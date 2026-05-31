"""
Shared pytest fixtures for canopen_tools.py tests.

The upstream CANopenNode example/ directory ships a reference DS301 profile in
both XDD (XML Device Description, CiA 311) and EDS (Electronic Data Sheet,
CiA 306) formats. These are the canonical test inputs used throughout the suite.
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

import pytest

# Make the repo root importable so we can import tools/canopen_tools.py
REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

# Reference files shipped with the upstream example
EXAMPLE_DIR = REPO_ROOT / "example"
XDD_FILE    = EXAMPLE_DIR / "DS301_profile.xpd"
EDS_FILE    = EXAMPLE_DIR / "DS301_profile.eds"


@pytest.fixture(scope="session")
def repo_root() -> Path:
    """Absolute path to the repository root."""
    return REPO_ROOT


@pytest.fixture(scope="session")
def example_dir() -> Path:
    """Absolute path to the upstream example/ directory."""
    assert EXAMPLE_DIR.is_dir(), f"example/ not found at {EXAMPLE_DIR}"
    return EXAMPLE_DIR


@pytest.fixture(scope="session")
def xdd_file() -> Path:
    """Path to the reference DS301 XDD profile (CiA 311 format)."""
    assert XDD_FILE.is_file(), f"XDD not found: {XDD_FILE}"
    return XDD_FILE


@pytest.fixture(scope="session")
def eds_file() -> Path:
    """Path to the reference DS301 EDS profile (CiA 306 format)."""
    assert EDS_FILE.is_file(), f"EDS not found: {EDS_FILE}"
    return EDS_FILE


@pytest.fixture()
def tmp_outdir(tmp_path: Path) -> Path:
    """A fresh temp directory for each test that generates output files."""
    return tmp_path
