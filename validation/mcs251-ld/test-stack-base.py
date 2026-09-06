#!/usr/bin/env python3
"""G12K128 self-start stack contract; no compiler/QEMU dependency."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("mld", Path(__file__).with_name("mcs251_ld.py"))
mld = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mld)


class StackBaseTest(unittest.TestCase):
    def linker(self, opt_in=True):
        linker = mld.Linker()
        if opt_in:
            symbol = linker.lkpsym("__mcs251_stack_base", create=True)
            symbol.ref_modules.append(mld.Head("crt.rel"))
        return linker

    def area(self, linker, name, ranges, flags=0):
        area = mld.Area(name)
        area.flag = flags
        for start, size in ranges:
            part = mld.Areax(area, None)
            part.addr, part.size = start, size
            area.areaxs.append(part)
        linker.areas.append(area)
        return area

    def value(self, linker, absolute=()):
        linker._define_mcs251_stack_base(absolute)
        return linker.symtab["__mcs251_stack_base"].addr

    def test_empty_data_skips_low_page(self):
        self.assertEqual(self.value(self.linker()), 0x10f)

    def test_slices_holes_and_overlay_use_max_end(self):
        linker = self.linker()
        self.area(linker, "DSEG", [(8, 12), (0x200, 0x31)])
        self.area(linker, "OSEG", [(0x240, 4), (0x240, 0x21)], mld.A3_OVR)
        self.assertEqual(self.value(linker), 0x27f)

    def test_non_edata_does_not_raise_highwater(self):
        linker = self.linker()
        self.area(linker, "CSEG", [(0xff0200, 0x200)], mld.A_CODE)
        self.area(linker, "XSEG", [(0x10000, 0x200)], mld.A_XDATA)
        self.area(linker, "BSEG", [(0x400, 4)], mld.A_BIT)
        self.assertEqual(self.value(linker), 0x10f)

    def test_absolute_origin_is_preserved(self):
        linker = self.linker()
        self.area(linker, "ABS_DATA", [(0, 8)], mld.A3_ABS)
        self.assertEqual(self.value(linker, [0x509]), 0x51f)

    def test_exactly_1k_available(self):
        linker = self.linker()
        self.area(linker, "DATA", [(0x100, 0xaf0)])
        self.assertEqual(self.value(linker), 0xbff)

    def test_insufficient_capacity_rejected(self):
        linker = self.linker()
        self.area(linker, "DATA", [(0x100, 0xaf1)])
        with self.assertRaisesRegex(mld.LinkError, "stack capacity: data end 0x0BF1 leaves fewer than 1024 bytes"):
            self.value(linker)

    def test_legacy_chain_unchanged(self):
        linker = self.linker(False)
        self.area(linker, "DATA", [(0x100, 0x3000)])
        before = [(p.addr, p.size) for a in linker.areas for p in a.areaxs]
        linker._define_mcs251_stack_base([])
        self.assertNotIn("__mcs251_stack_base", linker.symtab)
        self.assertEqual(before, [(p.addr, p.size) for a in linker.areas for p in a.areaxs])

    def test_user_cannot_override_reserved_symbol(self):
        linker = self.linker()
        linker.symtab["__mcs251_stack_base"].defined = True
        with self.assertRaisesRegex(mld.LinkError, "reserved for the linker"):
            self.value(linker)


if __name__ == "__main__":
    unittest.main()
