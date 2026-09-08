# -*- Python -*-
# Configuration file for the 'mcs251-isr-qualification' lit test suite.
#
# Reference: llvm/test/lit.cfg.py and lld/test/lit.cfg.py (read-only).
# This is a standalone suite: it lives under validation/ and is not
# discovered by any build-tree lit configuration. Per the T10 card, only the
# self-test RUN belongs here; the real qualification runs are executed
# manually via
#
#     python3 validation/mcs251-isr/qualify.py --manifest ABS_JSON --case ...
#
# and must never hang off the default lit so that no build ever starts QEMU
# or touches serial ports as a side effect.

import os
import sys

import lit.formats

# name: The name of this test suite.
config.name = "mcs251-isr-qualification"

# testFormat: shell-style RUN lines.
config.test_format = lit.formats.ShTest()

# suffixes: file extensions treated as tests. The T10 card freezes '.test'.
config.suffixes = [".test"]

# test_source_root / test_exec_root: this suite runs in place; the self-test
# writes no files and needs no scratch directory.
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = config.test_source_root

# 'Output' is lit's scratch directory for test runs (same convention as
# llvm/test and lld/test); never treat it as test input.
config.excludes = ["Output"]

# Standalone suite: lit.llvm's use_default_substitutions() is unavailable
# here, so %python must be registered explicitly. lit applies longer
# substitution keys first, so this wins over the builtin '%p' (source dir).
config.substitutions.append(("%python", '"%s"' % sys.executable))

# FileCheck is discovered through PATH. To point lit at a build tree's tool
# directory without installing anything, pass:
#     llvm-lit --param mcs251_tools_dir=/abs/build/bin validation/mcs251-isr
tools_dir = lit_config.params.get("mcs251_tools_dir")
if tools_dir:
    config.environment["PATH"] = (
        tools_dir + os.pathsep + config.environment.get("PATH", "")
    )
