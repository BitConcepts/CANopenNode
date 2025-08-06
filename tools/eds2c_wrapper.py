"""
EDS to C Conversion Script (CLI Wrapper)

This script provides a minimal command-line interface (CLI) for invoking the 
`eds2c` module from the `eds-utils` Python package. It allows Windows users 
to generate C source files from an EDS (Electronic Data Sheet) file without 
requiring GTK or other GUI dependencies, which are typically only supported 
on Linux or macOS.

Usage:
    python eds2c_wrapper.py <arguments>

Arguments are passed directly to the `eds2c` entry point. For example:
    python eds2c_wrapper.py generate path/to/file.eds -o output/dir

Note:
- This bypasses GUI-related modules like `gi` and `eds_editor.main`, which 
  often cause issues in Windows environments lacking GTK.
- Make sure `eds-utils` is installed in your Python environment.
"""

import sys
from eds_utils import eds2c


# Entry point for script execution
if __name__ == "__main__":
    # Pass command-line arguments to the eds2c CLI function
    eds2c.eds2c(sys.argv[1:])
