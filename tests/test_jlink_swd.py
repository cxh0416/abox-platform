import contextlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))
import jlink_swd as swd


class FakeDap:
    """AHB-AP with real 1 KiB TAR wrapping and Cortex-M3 debug side effects."""
    def __init__(self, halted=False):
        self.memory = {
            swd.UID: 0x05D3FF34, swd.UID + 4: 0x3733574E, swd.UID + 8: 0x43043550,
            swd.DEMCR: 0x01000000, swd.DBGMCU_CR: 0x8000, swd.FP_CTRL: 0x260,
        }
        self.cpu = {index: 0x08008000 + index * 4 for index in range(21)}
        self.cpu.update({14: 0xFFFFFFFD, 17: 0x20001000, 18: 0x20002000})
        frame = [1, 2, 3, 4, 12, 0x65, 0xA5A5A5A4, 0x01000000]
        for index, value in enumerate(frame):
            self.memory[0x20002000 + 4 * index] = value
        self.halted = halted
        self.control = 1
        self.ready = True
        self.csw = self.tar = 0
        self.dap_writes, self.writes = [], []
        self.closed = False
        self.fail_address = None
        self.capture_after = None
        self.remaining = None
        self.reset_pending = 0
        self.reset_reads = 0
        self.interrupt_address = None

    def connect(self, speed):
        self.speed = speed

    def close(self):
        self.closed = True

    def read_dap(self, index, ap=1):
        if (index, ap) != (3, 1):
            raise AssertionError((index, ap))
        address = self.tar
        if self.remaining is not None and address == swd.DHCSR:
            self.remaining -= 1
            if self.remaining <= 0:
                self.halted = True
                self.remaining = None
        if address == swd.DHCSR:
            reset_status = 0
            if self.reset_pending:
                self.reset_pending -= 1
                self.reset_reads += 1
                if self.reset_pending == 0:
                    reset_status = swd.S_RESET_ST
                    self.halted = bool(self.memory[swd.DEMCR] & 1)
            value = self.control | (swd.S_HALT if self.halted else 0) | (swd.S_REGRDY if self.ready else 0)
            value |= reset_status
        else:
            value = self.memory.get(address, address ^ 0x12345678)
            if swd.FP_COMP <= address < swd.FP_COMP + 24:
                value = self.memory.get(address, 0)
        if self.csw == 0x23000052:
            self.tar = (self.tar & ~1023) | ((self.tar + 4) & 1023)
        return value

    def write_dap(self, index, value, ap=1):
        self.dap_writes.append((index, value, ap))
        if ap == 0:
            assert (index, value) == (2, 0)
        elif index == 0:
            self.csw = value
        elif index == 1:
            self.tar = value
        elif index == 3:
            address = self.tar
            self.writes.append((address, value))
            if address == swd.DHCSR:
                self.control = value & 15
                self.halted = bool(value & 2)
                if not self.halted and self.memory[swd.DEMCR] & 0x400:
                    self.remaining = self.capture_after
            elif address == swd.DCRSR:
                self.memory[swd.DCRDR] = self.cpu[value]
            elif address == swd.FP_CTRL:
                self.memory[address] = (self.memory[address] & ~1) | (value & 1)
            elif address == swd.AIRCR:
                self.reset_pending = 2
            else:
                self.memory[address] = value
            if address == self.fail_address:
                self.fail_address = None
                raise swd.DebugError("injected partial write failure")
            if address == self.interrupt_address:
                self.interrupt_address = None
                raise KeyboardInterrupt()
        else:
            raise AssertionError(index)


def args(*values):
    return swd.parser().parse_args(list(values))


class SwdTest(unittest.TestCase):
    def run_action(self, backend, *values):
        with contextlib.redirect_stdout(io.StringIO()):
            swd.run(args(*values), backend)

    def test_read_crosses_tar_boundary_and_preserves_little_endian_bytes(self):
        backend = FakeDap()
        backend.memory.update({0x200003FC: 0x44332211, 0x20000400: 0x88776655})
        self.assertEqual(swd.Swd(backend).read(0x200003FD, 6), bytes.fromhex("223344556677"))
        self.assertEqual([value for index, value, ap in backend.dap_writes if index == 1],
                         [0x200003FC, 0x20000400])

    def test_invalid_ranges_never_access_dap(self):
        backend = FakeDap()
        debugger = swd.Swd(backend)
        for address, size in ((-1, 4), (0, 0), (0, -1), (0, 1048577), (0xFFFFFFFF, 2)):
            with self.assertRaises(swd.DebugError):
                debugger.read(address, size)
        self.assertEqual(backend.dap_writes, [])

    def test_raw_write_uses_no_increment_csw(self):
        backend = FakeDap()
        swd.Swd(backend).write32(0x20000000, 0x12345678)
        self.assertEqual(backend.dap_writes,
                         [(2, 0, 0), (0, 0x23000002, 1), (1, 0x20000000, 1), (3, 0x12345678, 1)])

    def test_uid_word_order_and_mismatch_prevents_core_changes(self):
        backend = FakeDap()
        self.assertEqual(swd.Swd(backend).uid(), "05D3FF343733574E43043550")
        with self.assertRaisesRegex(swd.DebugError, "UID mismatch"):
            self.run_action(backend, "--expect-uid", "0" * 24, "fault")
        self.assertEqual(backend.writes, [])
        self.assertTrue(backend.closed)

    def test_status_and_read_do_not_halt_reset_or_change_debug_configuration(self):
        for action in (("status",), ("read", "0x08008000", "16")):
            backend = FakeDap()
            self.run_action(backend, *action)
            self.assertEqual(backend.writes, [])
            self.assertTrue(backend.closed)

    def test_register_capture_restores_running_and_existing_halted_states(self):
        for halted in (False, True):
            backend = FakeDap(halted)
            self.run_action(backend, "regs")
            self.assertEqual(backend.halted, halted)
            self.assertEqual(backend.memory[swd.DEMCR], 0x01000000)
            self.assertEqual(backend.memory[swd.DBGMCU_CR], 0x8000)
            self.assertTrue(backend.closed)

    def test_exception_frame_uses_psp_and_preserves_fault_pc(self):
        backend = FakeDap(True)
        snapshot = swd.Swd(backend).snapshot()
        self.assertEqual(snapshot["exception_stack"], 0x20002000)
        self.assertEqual(snapshot["exception_frame"]["pc"], 0xA5A5A5A4)
        self.assertEqual(snapshot["registers"]["psp"], 0x20002000)

    def test_exception_frame_uses_msp_only_for_valid_exc_return_and_ram(self):
        backend = FakeDap(True)
        backend.cpu[14], backend.cpu[17] = 0xFFFFFFF9, 0x20002000
        self.assertIn("exception_frame", swd.Swd(backend).snapshot())
        for lr, stack in ((0x08008001, 0x20002000), (0xFFFFFFF9, 0x2000FFFF),
                          (0xFFFFFFF9, 0x08008000)):
            backend.cpu[14], backend.cpu[17] = lr, stack
            self.assertNotIn("exception_frame", swd.Swd(backend).snapshot())

    def test_register_transfer_timeout_is_bounded_and_restores_running(self):
        backend = FakeDap()
        backend.ready = False
        with self.assertRaisesRegex(swd.DebugError, "Register transfer timed out"):
            self.run_action(backend, "--io-timeout", "0.01", "fault")
        self.assertFalse(backend.halted)
        self.assertEqual(backend.memory[swd.DBGMCU_CR], 0x8000)
        self.assertTrue(backend.closed)

    def test_partial_prepare_failure_still_restores_state(self):
        backend = FakeDap()
        backend.fail_address = swd.DBGMCU_CR
        with self.assertRaises(swd.DebugError):
            self.run_action(backend, "fault")
        self.assertEqual(backend.memory[swd.DBGMCU_CR], 0x8000)
        self.assertFalse(backend.halted)
        self.assertTrue(backend.closed)

    def test_capture_timeout_restores_state_without_reset_or_retry(self):
        backend = FakeDap()
        with self.assertRaisesRegex(swd.DebugError, "capture timed out"):
            self.run_action(backend, "capture", "--timeout", "0.01")
        self.assertNotIn(swd.AIRCR, [address for address, value in backend.writes])
        self.assertFalse(backend.halted)
        self.assertEqual(backend.memory[swd.DEMCR], 0x01000000)
        self.assertTrue(backend.closed)

    def test_explicit_reset_waits_for_new_reset_and_only_resets_once(self):
        backend = FakeDap()
        backend.capture_after = 2
        self.run_action(backend, "capture", "--reset")
        self.assertEqual(backend.reset_reads, 2)
        self.assertEqual([value for address, value in backend.writes if address == swd.AIRCR],
                         [0x05FA0004])
        self.assertFalse(backend.halted)
        self.assertEqual(backend.memory[swd.DEMCR], 0x01000000)

    def test_interrupt_after_halt_restores_state_and_closes_backend(self):
        backend = FakeDap()
        backend.interrupt_address = swd.DCRSR
        with self.assertRaises(KeyboardInterrupt):
            self.run_action(backend, "fault")
        self.assertFalse(backend.halted)
        self.assertEqual(backend.memory[swd.DBGMCU_CR], 0x8000)
        self.assertTrue(backend.closed)

    def test_fpb_lower_and_upper_halfword_encoding_and_restoration(self):
        for address, expected in ((0x0800CBA4, 0x4800CBA5), (0x0800CBA7, 0x8800CBA5)):
            backend = FakeDap()
            backend.memory[swd.FP_COMP] = 0x48008001  # Preserve an existing comparator.
            backend.capture_after = 2
            self.run_action(backend, "capture", "--breakpoint", hex(address))
            self.assertIn((swd.FP_COMP + 4, expected), backend.writes)
            self.assertEqual(backend.memory[swd.FP_COMP], 0x48008001)
            self.assertEqual(backend.memory[swd.FP_COMP + 4], 0)
            self.assertFalse(backend.memory[swd.FP_CTRL] & 1)
            self.assertFalse(backend.halted)

    def test_no_free_comparator_restores_state(self):
        backend = FakeDap()
        for index in range(6):
            backend.memory[swd.FP_COMP + 4 * index] = 0x48008001
        with self.assertRaisesRegex(swd.DebugError, "No free FPB"):
            self.run_action(backend, "capture", "--breakpoint", "0x08008000")
        self.assertFalse(backend.halted)
        self.assertEqual(backend.memory[swd.DEMCR], 0x01000000)

    def test_finish_attempts_resume_even_if_other_restoration_fails(self):
        backend = FakeDap()
        debugger = swd.Swd(backend)
        debugger.prepare()
        debugger.halt()
        backend.fail_address = swd.DEMCR
        with self.assertRaisesRegex(swd.DebugError, "State restoration failed"):
            debugger.finish()
        self.assertFalse(backend.halted)
        self.assertEqual(backend.memory[swd.DBGMCU_CR], 0x8000)

    def test_cleanup_clears_comparators_catches_freeze_and_halt(self):
        backend = FakeDap(True)
        backend.memory[swd.DEMCR] |= 0x401
        backend.memory[swd.DBGMCU_CR] |= 0x307
        backend.memory[swd.FP_COMP] = 0x48008001
        self.run_action(backend, "cleanup")
        self.assertFalse(backend.halted)
        self.assertEqual(backend.control, 0)
        self.assertEqual(backend.memory[swd.DEMCR], 0x01000000)
        self.assertEqual(backend.memory[swd.DBGMCU_CR], 0x8000)
        self.assertEqual(backend.memory[swd.FP_COMP], 0)

    def test_cleanup_attempts_resume_after_comparator_write_failure(self):
        backend = FakeDap(True)
        backend.fail_address = swd.FP_COMP
        with self.assertRaisesRegex(swd.DebugError, "Cleanup incomplete"):
            self.run_action(backend, "cleanup")
        self.assertFalse(backend.halted)
        self.assertEqual(backend.control, 0)
        self.assertTrue(backend.closed)

    def test_binary_output_is_explicit_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "sample.dat"
            self.run_action(FakeDap(), "read", hex(swd.UID), "12", "--output", str(path))
            before = path.read_bytes()
            backend = FakeDap()
            with self.assertRaises(FileExistsError):
                self.run_action(backend, "read", hex(swd.UID), "12", "--output", str(path))
            self.assertEqual(path.read_bytes(), before)
            self.assertTrue(backend.closed)

    def test_cli_requires_identity_before_any_core_state_operation(self):
        for action in ("regs", "fault", "capture", "cleanup"):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                swd.main([action])

    def test_dap_error_returns_are_reported_without_retries(self):
        backend = object.__new__(swd.Segger)
        calls = []
        def failure(*values):
            calls.append(values)
            return -1
        backend.lib = types.SimpleNamespace(JLINKARM_CORESIGHT_ReadAPDPReg=failure,
                                            JLINKARM_CORESIGHT_WriteAPDPReg=failure)
        with self.assertRaisesRegex(swd.DebugError, "DAP read"):
            backend.read_dap(3)
        with self.assertRaisesRegex(swd.DebugError, "DAP write"):
            backend.write_dap(1, 0x20000000)
        self.assertEqual(len(calls), 2)


if __name__ == "__main__":
    unittest.main()
