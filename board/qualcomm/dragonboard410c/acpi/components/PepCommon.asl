//
// The PEP Device & Driver Related Configuration
//

Device (PEP0)
{

    Name (_HID, "QCOM2425")
    Name (_CID, "PNP0D80")

    Name(_CRS, ResourceTemplate ()
    {
        // List interrupt resources in the order they are used in PEP_Driver.c

        // TSENS threshold interrupt
        Interrupt(ResourceConsumer, Level, ActiveHigh, ExclusiveAndWake, , , ) {216}

        // Inbound interrupt from rpm:
        //rpm_to_kpss_ipc_irq0 = SYSApcsQgicSpi168 = 200
        Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {200}

        //rpm_to_kpss_ipc_irq0 = SYSApcsQgicSpi169 = 201   (MPM)
        Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {201}

        //rpm_to_kpss_ipc_irq0 = SYSApcsQgicSpi171 = 203   (wakeup)
        Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {203}

        // CPR
        Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {47}

    })

    // Need 20 char and 1 D state info
    Field(\_SB.ABD.ROP1, BufferAcc, NoLock, Preserve)
    {
        /* Connection Object  - 0x007C is the unique identifier */
        Connection(I2CSerialBus( 0x0001,,0x0,, "\\_SB.ABD",,,,)),
        AccessAs(BufferAcc, AttribRawBytes(21)),
        FLD0, 168
    }

    // Get port to connect to
    Method(GEPT)
    {
        Name(BUFF, Buffer(4){})
        CreateByteField(BUFF, 0x00, STAT)
        CreateWordField(BUFF, 0x02, DATA)
        Store(0x1, DATA) // In this example we will connect to ABDO
        Return(DATA)
    }

}