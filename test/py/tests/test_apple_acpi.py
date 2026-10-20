# SPDX-License-Identifier: GPL-2.0+
"""Check the model-selected Apple ACPI bring-up tables."""

import re
import shutil
import struct
import subprocess

import pytest

pytestmark = [
    pytest.mark.buildconfigspec('arch_apple'),
    pytest.mark.buildconfigspec('generate_acpi_table'),
    pytest.mark.buildconfigspec('cmd_acpi'),
    pytest.mark.buildconfigspec('cmd_fdt'),
]


def _is_j413(ubman):
    ubman.run_command('fdt addr -c')
    output = ubman.run_command('fdt print / compatible')
    return '"apple,j413"' in output


def _table(ubman, signature):
    output = ubman.run_command('acpi dump ' + signature)
    data = bytearray()
    for line in output.splitlines():
        match = re.match(r'^[0-9a-fA-F]+:\s+((?:[0-9a-fA-F]{2}\s+)+)', line)
        if match:
            data.extend(bytes.fromhex(match[1]))
    assert data[:4] == signature.encode()
    assert len(data) == struct.unpack_from('<I', data, 4)[0]
    assert sum(data) % 256 == 0
    assert data[10:16] == b'AAPL  '
    assert data[16:24] == b'AAPLUBOT'
    return data


def test_apple_acpi_model_selection(ubman):
    """Only an explicitly registered model gets startup ACPI tables."""
    supported = _is_j413(ubman)
    output = ubman.run_command('acpi list -c')
    if not supported:
        assert 'No ACPI tables present' in output
        return

    names = set(re.findall(r'^([A-Z0-9]{4})\s', output, re.MULTILINE))
    assert names == {'RSDP', 'XSDT', 'DSDT', 'FACP', 'APIC', 'GTDT', 'CSRT', 'DBG2'}
    assert 'bad' not in output


def test_apple_acpi_j413_tables(ubman):
    """Check CPU identity, stub controller, timers, debug UART and FADT."""
    if not _is_j413(ubman):
        pytest.skip('Requires apple,j413')

    fadt = _table(ubman, 'FACP')
    assert struct.unpack_from('<I', fadt, 112)[0] == 1 << 20
    assert struct.unpack_from('<H', fadt, 129)[0] & 1
    assert struct.unpack_from('<Q', fadt, 132)[0] == 0  # No FACS.
    assert struct.unpack_from('<Q', fadt, 140)[0] != 0  # 64-bit DSDT.

    madt = _table(ubman, 'APIC')
    assert madt[36:44] == bytes(8)
    assert len(madt) == 44 + 8 * 80
    mpidrs = [0, 1, 2, 3, 0x10100, 0x10101, 0x10102, 0x10103]
    for uid, mpidr in enumerate(mpidrs):
        cpu = madt[44 + uid * 80:44 + (uid + 1) * 80]
        assert cpu[:2] == bytes([11, 80])
        assert struct.unpack_from('<III', cpu, 4) == (uid, uid, 1)
        assert struct.unpack_from('<II', cpu, 16) == (0, 0x17)
        assert cpu[24:56] == bytes(32)
        assert struct.unpack_from('<IQQ', cpu, 56) == (0x19, 0, mpidr)
        assert cpu[76:] == bytes([int(uid >= 4), 0, 0, 0])

    gtdt = _table(ubman, 'GTDT')
    assert len(gtdt) == 96 and gtdt[8] == 2
    assert struct.unpack_from('<QI', gtdt, 36) == (2**64 - 1, 0)
    assert struct.unpack_from('<8I', gtdt, 48) == (29, 4, 30, 4, 27, 4, 26, 4)
    assert struct.unpack_from('<QII', gtdt, 80) == (2**64 - 1, 0, 0)

    csrt = _table(ubman, 'CSRT')
    assert len(csrt) == 104
    assert struct.unpack_from('<I4sIHHHHI', csrt, 36) == (
        68, b'AAPL', 0, 0x100a, 0, 2, 0, 28)
    assert struct.unpack_from('<HHII', csrt, 60) == (2, 0, 0x3b0c0000, 2)
    assert csrt[72:88] == bytes(16)
    assert struct.unpack_from('<IHHII', csrt, 88) == (16, 1, 1, 0, 0)

    dbg2 = _table(ubman, 'DBG2')
    offset, count = struct.unpack_from('<II', dbg2, 36)
    assert count == 1
    device = dbg2[offset:]
    assert struct.unpack_from('<HH', device, 12) == (0x8000, 0x16)
    address_offset, size_offset = struct.unpack_from('<HH', device, 18)
    assert struct.unpack_from('<BBBBQ', device, address_offset) == (
        0, 32, 0, 3, 0x235200000)
    assert struct.unpack_from('<I', device, size_offset)[0] == 0x1000
    path_size, path_offset = struct.unpack_from('<HH', device, 4)
    assert device[path_offset:path_offset + path_size] == b'\\_SB.UART\0'


def test_apple_acpi_j413_dsdt(ubman, tmp_path):
    """Disassemble the actual DSDT and check its namespace and 64-bit UART."""
    if not _is_j413(ubman):
        pytest.skip('Requires apple,j413')
    iasl = shutil.which('iasl')
    if not iasl:
        pytest.skip('iasl is required for DSDT disassembly')

    aml = tmp_path / 'dsdt.aml'
    aml.write_bytes(_table(ubman, 'DSDT'))
    subprocess.run([iasl, '-d', str(aml)], check=True, capture_output=True)
    dsdt = aml.with_suffix('.dsl').read_text()
    assert 'Device (CPUS)' in dsdt
    assert dsdt.count('"ACPI0007"') == 8
    for uid in range(8):
        cpu = re.search(rf'Device \(CPU{uid}\)\s*\{{([^{{}}]+)\}}', dsdt)[1]
        value = re.search(r'Name \(_UID, (Zero|One|0x[0-9A-F]+)\)', cpu)[1]
        actual = int(value == 'One') if value in ('Zero', 'One') else int(value, 16)
        assert actual == uid
    assert '"ACPI0010"' in dsdt
    assert 'Device (UART)' in dsdt
    assert '"APP0001"' in dsdt
    assert '0x0000000235200000' in dsdt
    assert '0x0000000235200FFF' in dsdt
