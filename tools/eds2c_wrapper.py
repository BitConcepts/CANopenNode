#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# Copyright (c) 2025 BitConcepts, LLC
# Author: BitConcepts, LLC <https://github.com/BitConcepts>
#
# This file is part of <https://github.com/CANopenNode/CANopenNode>, a CANopen stack.
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

Notes:
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
