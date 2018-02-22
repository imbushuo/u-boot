//
// Copyright (c) 2014, Qualcomm Technologies Inc. All rights reserved.
//
// This file contains the Bus Access Modules (BAM)
// ACPI device definitions and pipe configurations
//

//
//  Device Map:
//    0x2401 - BAM
//

Device (BAM1)
{
    Name (_HID, "QCOM2401")
    Name (_UID, 1)
    Method (_CRS, 0x0, NotSerialized) {
        Name (RBUF, ResourceTemplate ()
        {
            // CRYPTO1 BAM register address space
            Memory32Fixed (ReadWrite, 0x00704000, 0x00020000)

            Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {239}
        })
        Return (RBUF)
    }
}

Device (BAM3)
{
    Name (_HID, "QCOM2401")
    Name (_UID, 3)
    Method (_CRS, 0x0, NotSerialized) {
        Name (RBUF, ResourceTemplate ()
        {
            // BLSP1 BAM register address space
            Memory32Fixed (ReadWrite, 0x07884000, 0x00023000)

            Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {270}
        })
        Return (RBUF)
    }
}

Device (BAMC)
{
    Name (_HID, "QCOM2401")
    Name (_UID, 12)
    Method (_CRS, 0x0, NotSerialized) {
        Name (RBUF, ResourceTemplate ()
        {
            // USB1_HS BAM register address space
            Memory32Fixed (ReadWrite, 0x078C4000, 0x00015000)

            Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {167}
        })
        Return (RBUF)
    }
}

Device (BAME)
{
    Name (_HID, "QCOM2401")
    Name (_UID, 14)
    Method (_CRS, 0x0, NotSerialized) {
        Name (RBUF, ResourceTemplate ()
        {
            // A2 BAM register address space
            Memory32Fixed (ReadWrite, 0x04044000, 0x00019000)

            Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {61}
        })
        Return (RBUF)
    }
}
