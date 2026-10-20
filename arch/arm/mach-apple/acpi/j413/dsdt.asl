// SPDX-License-Identifier: GPL-2.0+

DefinitionBlock ("", "DSDT", 2, "AAPL  ", "AAPLUBOT", 1)
{
    Scope (\_SB)
    {
        Device (CPUS)
        {
            Name (_HID, "ACPI0010")
            Name (_UID, Zero)

            Device (CPU0) { Name (_HID, "ACPI0007") Name (_UID, 0) }
            Device (CPU1) { Name (_HID, "ACPI0007") Name (_UID, 1) }
            Device (CPU2) { Name (_HID, "ACPI0007") Name (_UID, 2) }
            Device (CPU3) { Name (_HID, "ACPI0007") Name (_UID, 3) }
            Device (CPU4) { Name (_HID, "ACPI0007") Name (_UID, 4) }
            Device (CPU5) { Name (_HID, "ACPI0007") Name (_UID, 5) }
            Device (CPU6) { Name (_HID, "ACPI0007") Name (_UID, 6) }
            Device (CPU7) { Name (_HID, "ACPI0007") Name (_UID, 7) }
        }

        Device (UART)
        {
            Name (_HID, "APP0001")
            Name (_UID, Zero)
            Name (_DDN, "Apple S5L debug UART")
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0, 0x235200000, 0x235200FFF, 0, 0x1000)
            })
        }
    }
}
