//
// UARTBAM1 (BLSP1 UART1) (4-wire UART on SBC)
//
Device (UAR1)
{
    Name (_HID, "QCOM2424")
    Name (_UID, 1)
    Method (_CRS)
    {
        Name (RBUF, ResourceTemplate()
        {
            Memory32Fixed(ReadWrite, 0x078af000, 0x00000100)
            Interrupt(ResourceConsumer, Level, ActiveHigh, Exclusive) {139}
            GpioInt(Edge, ActiveLow, Exclusive, PullDown, 0, "\\_SB.GIO0") {1}  // UART RX
        })
        Return (RBUF)
    }
    Method (PROP)
    {
        Name (RBUF, Package()
        {
            "UartClass",            1,  // 0 = UART_DM, 1 = UART_BAM
            "EnableDma",            0,
            "BamBase",     0x07884000,
            "BamPipeTx",            0,
            "BamPipeRx",            1,
            "QgicBase",    0x0b000000,  // for debug purposes
            "ClkrgmBase",  0x01800000,  // for debug purposes
            "TlmmBase",    0x01000000,  // for debug purposes
            "GpioRfrN",             3,  // for debug purposes
            "GpioCtsN",             2,  // for debug purposes
            "GpioRxData",           1,  // for debug purposes
            "GpioTxData",           0,  // for debug purposes
        })
        Return (RBUF)
    }
}

//
// UARTBAM2 (BLSP1 UART2) (loopback testing on DE9 port)
//
Device (UAR2)
{
    Name (_HID, "QCOM2424")
    Name (_UID, 2)
    Method (_CRS)
    {
        Name (RBUF, ResourceTemplate()
        {
            Memory32Fixed(ReadWrite, 0x078b0000, 0x00000100)
            Interrupt(ResourceConsumer, Level, ActiveHigh, Exclusive) {140}
            GpioInt(Edge, ActiveLow, Exclusive, PullDown, 0, "\\_SB.GIO0") {5}  // UART RX
        })
        Return (RBUF)
    }
    Method (PROP)
    {
        Name (RBUF, Package()
        {
            "UartClass",            1,  // 0 = UART_DM, 1 = UART_BAM
            "EnableDma",            0,
            "BamBase",     0x07884000,
            "BamPipeTx",            2,
            "BamPipeRx",            3,
            "QgicBase",    0x0b000000,  // for debug purposes
            "ClkrgmBase",  0x01800000,  // for debug purposes
            "TlmmBase",    0x01000000,  // for debug purposes
            "GpioRxData",           5,  // for debug purposes
            "GpioTxData",           4,  // for debug purposes
        })
        Return (RBUF)
    }
}
