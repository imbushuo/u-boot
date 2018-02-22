DefinitionBlock ("", "DSDT", 2, "QCOMM ", "APQ8016 ", 0x00000003)
{
	External (_SB_.PMBM, UnknownObj)

	Scope (\_SB)
	{
		// These should be overwritten by UEFI with the correct values.
		// However, we are lazy, so we hard-coded known values

		/* Assume we are on APQ8016 */
		Name (SOID, 0xF7)
        Name (SIDS, "801600000000000")
        Name (SIDV, 0xFFFFFFFF)
        Name (SVMJ, 0xFFFF)
        Name (SVMI, 0xFFFF)
        Name (SDFE, 0xFFFF)
        Name (SFES, "801600000000000")
        Name (SIDM, 0xFFFFFFFF)

		/* rmtfs@86700000, qcom,rmtfs-mem and more */
		Name (RMTB, 0x86700000)
		Name (RMTX, 0x06C00000)

		// TLMM GPIO controller
		Device (GIO0)
        {
            Name (_HID, "QCOM2405")
            Name (_UID, 0)

            Method (_CRS, 0x0, NotSerialized) 
            {
                Name (RBUF, ResourceTemplate ()
                {
                   // TLMM register address space
                    Memory32Fixed (ReadWrite, 0x01000000, 0x00300000)

                    // Summary Interrupt shared by all banks
                    Interrupt(ResourceConsumer, Level, ActiveHigh, Shared, , , ) {240}
                    Interrupt(ResourceConsumer, Level, ActiveHigh, Shared, , , ) {240}
                })
                Return (RBUF)
            }

            // ACPI method to return Num pins
            Method(OFNI, 0x0, NotSerialized) 
            {
                Name(RBUF, Buffer()
                {    
                    0x7A,  // 0: TOTAL_GPIO_PINS
                    0x00   // 1: TOTAL_GPIO_PINS
                })
                Return (RBUF)
            }
        }

        // CPUs
        Device(CPU0)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 0)
        }
        Device(CPU1)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 1)
        }
        Device(CPU2)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 2)
        }
        Device(CPU3)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 3)
        }

        // ABD
        Device (ABD)
        {
            Name (_HID, "QCOM2431")
            Name (_UID, 0)
            OperationRegion(ROP1, GenericSerialBus, 0x00000000, 0x100)
            Name(AVBL, Zero)
            Method(_REG, 0x2, NotSerialized)
            {
                If(Lequal(Arg0, 0x9))
                {
                    Store(Arg1, AVBL)
                }
            }
        }

        // SMMU and BAM
        Include("components/BearSmmu.asl")
        Include("components/Bam.asl")
        Include("components/Scm.asl")

        // PMIC and PEP
        Include("components/Pmic.asl")
        Include("components/PepCommon.asl")

        // UART
        Include("components/Buses.asl")

        // USB
        Device (URS0)
        {
            Name (_HID, "QCOM24B7")
            Name(_DEP, Package(0x1)
            {
                \_SB_.PEP0
            })
            Name(_CCA, Zero) // Cache-incoherent bus-master, Hardware does not manage cache coherency
            Name(_S0W, 3)
            Name (REG, 0)    // Declare register object

            Name(_CRS, ResourceTemplate() 
            {
                // Controller register address space
                Memory32Fixed (ReadWrite, 0x078D9000, 0x00000300)
                // VBUS GPIO IRQ (cable attach/detach)
                GpioInt(Edge, ActiveBoth, ExclusiveAndWake, PullDown, 0, "\\_SB.GIO0") {0x79} // 121
            })

            // Controller register memory
            OperationRegion (UCDR, SystemMemory, 0x078D9000, 0x00001000 )
            Field (UCDR, WordAcc, NoLock, Preserve)
            {
                Offset(0x44),   // skip to register 44h
                SCRA, 32,       // scratch
                Offset(0x90),   // skip to register 90h
                AHBB, 32,       // Ahb_burst
                Offset(0x98),   // skip to register 98h
                AHBM, 32,       // Ahb_mode
                Offset(0x9C),   // skip to register 9Ch
                GENC, 32,       // Gen_config
                Offset(0xA0),   // skip to register A0h
                GETC, 32,       // Gen_config
                Offset(0x140),  // skip to register 140h
                UCMD, 32,      // USB command reg
                Offset(0x170),  // skip to register 170h
                ULPI, 32,       // ULPI port access
                Offset(0x184),  // skip to register 184h
                PTSC, 32,       // port status control
                Offset(0x1A8),  // skip to register 1A8h
                MODE, 32,        // usb mode
                Offset(0x278),
                CTRL, 32
            }

            // TLMM GPIO register memory
            OperationRegion (TCDR, SystemMemory, 0x01000000, 0x00300000)
            Field (TCDR, DWordAcc, NoLock, Preserve)
            {
                // GPIO #121
                Offset(0x00079000),   // skip to register h00
                GCFG, 32,             // GPIO configuraiton
                Offset(0x00079004),   // skip to register h04
                GSTS, 32,             // GPIO status
                Offset(0x00079008),   // skip to register h08
                ICFG, 32,             // GPIO Interrupt configuration
                Offset(0x0007900C),   // skip to register h0C
                ISTS, 32,             // GPIO Interrupt status
            }

            // Device Specific Method takes 4 args:
            // Arg0 : Buffer containing a UUID [16 bytes]
            // Arg1 : Integer containing the Revision ID
            // Arg2 : Integer containing the Function Index
            // Arg3 : Package that contains function-specific arguments
            Method (_DSM, 0x4, NotSerialized)
            {

                Name (RET, 0);  // Name (RET, Buffer(1){0}); // Declare return object

                // UUID selector
                switch(ToBuffer(Arg0)) 
                {
                    // URS interface identifier
                    case(ToUUID("14EB0A6A-79ED-4B37-A8C7-84604B55C5C3")) 
                    {
                        // Function selector
                        switch(Arg2) 
                        {
                            // Function 0: Return supported functions, based on revision
                            case(0) 
                            {
                                switch(Arg1) 
                                {
                                    // Revision0: function {1,2} supported
                                    case(0) { Return(0x03); Break; }
                                    default { Return(Zero); Break; }
                                }
                            // default
                            Return (0x00); Break;
                            }

                            // Function 1: Initialize VBUS interrupt, return VBUS low
                            case(1) 
                            {
                                // no configuration required on this target
                                Store(GSTS, REG);               // read GPIO status
                                And(REG, 0x01, REG);            // mask value (bit0); 0=grounded, 1=asserted
                                Store(LEqual(REG,0), RET);      // set boolean return value
                                Return(RET); 
                                Break;
                            }

                            // Function 2: Handle VBUS interrupt, return VBUS low
                            case(2) 
                            {
                                // no handling required on this target
                                Store(GSTS, REG);                // read GPIO status
                                And(REG, 0x01, REG);             // mask value (bit0); 0=grounded, 1=asserted
                                Store(LEqual(REG,0), RET);       // set boolean return value
                                Return(RET); 
                                Break;
                            }

                            default { Return(Zero); Break; }
                        } // Function
                        Break;
                    }
                    default { Return(Zero); Break; }
                } // UUID

            } // _DSM

            // Dynamically enumerated device (host mode stack) on logical USB bus
            Device(USB0)
            {
                Name(_ADR, 0)
                Name(_S0W, 3)
                Name(_CRS, ResourceTemplate() 
                {
                    // Interrupt usb1_hs_irq
                    Interrupt(ResourceConsumer, Level, ActiveHigh, Exclusive, , , ) {0xA6}
                    // Interrupt usb1_hs_async_wakeup_irq
                    Interrupt(ResourceConsumer, Level, ActiveHigh, ExclusiveAndWake, ) {0xAC}
                    // PMIC GPIO#3 - USB Hub reset
                    GpioIo(Exclusive, PullUp, 0, 0, , "\\_SB.PM01", , , , RawDataBuffer() { 0x1, 0xFF, 0xFF, 0xFF}) {0x610}
                    // PMIC GPIO#4 - USB MUX software select
                    GpioIo(Exclusive, PullUp, 0, 0, , "\\_SB.PM01", , , , RawDataBuffer() { 0x1, 0xFF, 0xFF, 0xFF}) {0x618}
                })

                //
                // _UBF method invoked from EHCI driver
                //
                Method (_UBF, 0x0, NotSerialized) 
                {
                    Store(0x13, MODE);               // set host mode & disable streaming  [HPG 5.3.7]
                    Store(PTSC, REG);                // read PORTSC status
                    Store(OR(REG,0x2),PTSC);         // clear current PORTSC status
                    Store(0x08, AHBM);               // use AHB xtor ctrl structs non-posted, data xfer posted  [HPG 5.3.2.2]
                    Store(0x00, AHBB);               // needs to be 0 if 0x98 is 0  [HPG 5.3.2.1]
                    Store(0x0C90, GENC);             // enable PE_RX_BUF_PENDING_EN, DSC_PE_RST_EN.  [HPG 5.3.2.3]
                }
            } // USB0

            // Dynamically enumerated device (peripheral mode stack) on logical USB bus
            Device(UFN0)
            {
                Name(_ADR, 1)
                Name(_S0W, 3)
                Name(_CRS, ResourceTemplate() 
                {
                    // Interrupt usb1_hs_irq
                    Interrupt(ResourceConsumer, Level, ActiveHigh, Exclusive, , , ) {0xA6}
                    // Interrupt usb1_hs_async_wakeup_irq
                    Interrupt(ResourceConsumer, Level, ActiveHigh, ExclusiveAndWake, ) {0xAC}
                    // Attach Interrupt
                    GpioInt(Edge, ActiveBoth, ExclusiveAndWake, PullUp, 0, "\\_SB.PM01",,,,RawDataBuffer(){0x21, 0x1, 0x2, 0x19}) {0x1002}
                    // Detach Interrupt
                    GpioInt(Edge, ActiveBoth, ExclusiveAndWake, PullUp, 0, "\\_SB.PM01",,,,RawDataBuffer(){0x20, 0x1, 0x2, 0x19}) {0x1001}
                    // PMIC GPIO#3 - USB Hub reset
                    GpioIo(Exclusive, PullUp, 0, 0, , "\\_SB.PM01", , , , RawDataBuffer() { 0x1, 0xFF, 0xFF, 0xFF}) {0x610}
                    // PMIC GPIO#4 - USB MUX software select
                    GpioIo(Exclusive, PullUp, 0, 0, , "\\_SB.PM01", , , , RawDataBuffer() { 0x1, 0xFF, 0xFF, 0xFF}) {0x618}
                })

                //
                // _UBF method invoked from USBFn driver
                //
                Method (_UBF, 0x0, NotSerialized) 
                {
                    Store(0x02, MODE);               // set device mode & disable streaming [HPG 5.3.7]
                    Store(PTSC, REG);                // read PORTSC status
                    Store(OR(REG,0x2),PTSC);         // clear current PORTSC status
                    Store(0x08, AHBM);               // use AHB xtor ctrl structs non-posted, data xfer posted [HPG 5.3.2.2]
                    Store(0x00, AHBB);               // needs to be 0 if 0x98 is 0 [HPG 5.3.2.1]
                    Store(0x0CB0, GENC);             // Enable PE_RX_BUF_PENDING_EN, DSC_PE_RST_EN. [HPG 5.3.2.3]
                    Store(0x000D3C32, CTRL);         // Enable external vbus configuration in the LINK.
                    Store(0x60960003, ULPI);         // Enable VBUSVLDEXTSEL and VBUSVLDEXT_SET [HPG 5.3.3, 5.3.9]
                    Store(GETC, REG);                // read GENC2 status
                    Store(OR(REG,0x80), GETC);       // Enable SESS_VLD [HPG 5.3.3]
                    Store(UCMD, REG);                // read USBCMD register
                    Store(OR(REG,0x2000000), UCMD);  // Set SESS_VLD bit 19 [HPG 5.3.3]
                    Store(0x39, SCRA);               // TBD
                }

                // Device Specific Method takes 4 args:
                //  Arg0 : Buffer containing a UUID [16 bytes]
                //  Arg1 : Integer containing the Revision ID
                //  Arg2 : Integer containing the Function Index
                //  Arg3 : Package that contains function-specific arguments
                Method (_DSM, 0x4, NotSerialized)
                {
                    // UUID selector
                    switch(ToBuffer(Arg0)) 
                    {
                        // UFX interface identifier
                        case(ToUUID("FE56CFEB-49D5-4378-A8A2-2978DBE54AD2")) 
                        {
                            // Function selector
                            switch(Arg2) 
                            {
                                // Function 0: Return supported functions, based on revision
                                case(0) 
                                {
                                    // Version selector
                                    switch(Arg1) 
                                    {
                                        // Revision0: functions {0,1} supported
                                        case(0) { Return(Buffer(){0x03}); Break; }
                                        default { Return(Buffer(){0x01}); Break; }
                                    }
                                    // default
                                    Return (Buffer(){0x00}); Break;
                                }

                                // Function 1: Return number of supported USB PHYSICAL endpoints
                                // ChipIdea core configured to support 8 IN/8 OUT EPs, including EP0
                                case(1) { Return(16); Break; }

                                default { Return (Buffer(){0x00}); Break; }
                            } // Function
                        } // {FE56CFEB-49D5-4378-A8A2-2978DBE54AD2}

                        // QCOM specific interface identifier
                        case(ToUUID("18DE299F-9476-4FC9-B43B-8AEB713ED751")) 
                        {
                            // Function selector
                            switch(Arg2) 
                            {
                                // Function 0: Return supported functions, based on revision
                                case(0) 
                                {
                                    // Version selector
                                    switch(Arg1) 
                                    {
                                        // Revision0: functions {0,1} supported
                                        case(0) { Return(Buffer(){0x03}); Break; }
                                        default { Return(Buffer(){0x01}); Break; }
                                    }
                                    // default 
                                    Return (Buffer(){0x00}); Break;
                                }

                                // Function 1: Return device capabilities bitmap
                                //   Bit  Description
                                //   ---  -------------------------------
                                //     0  Superspeed Gen1 supported
                                //     1  PMIC VBUS detection supported
                                //     2  USB PHY interrupt supported
                                //     3  Type-C supported
                                case(1) { Return(0x02); Break; }

                                default { Return (Buffer(){0x00}); Break; }
                            } // Function
                        } // {18DE299F-9476-4FC9-B43B-8AEB713ED751}

                        default { Return (Buffer(){0x00}); Break; }
                    } // UUID
                } // _DSM
            }

            //
            // The recommended PHY register values. The following values of PHY
            // will be configured if OEMs do not overwrite the values.
            //
            Method(PHYC, 0x0, NotSerialized) 
            {
                Name (CFG0, Package()
                {
                    // PHY_IFC, PHY REG ADDR, Value
                    Package() 
                    {
                        0x0,
                        0x80,
                        0x74
                    }
                    //ULPI, USB_OTG_HS_PARAM_OVER_REG_A_ADDR, USB_OTG_HS_PARAM_OVER_REG_A_RST_VAL
                })
                Return (CFG0)
            }
        }

        // GFX

        // SPMI
        Device(SPMI)
        {
           Name(_HID, "QCOM2404")
           Name(_UID, One)
           Method(_CRS, 0x0, NotSerialized)
           {
              Name(RBUF, ResourceTemplate ()
              {
                 Memory32Fixed(ReadWrite, 0x02000000, 0x01200000)
              })
              Return(RBUF)
           }
           Method (CONF)
           {
               Name (XBUF, Buffer ()
               {
                   0x0, // uThisOwnerNumber
                   0x1  // Polling Mode  
               })
               Return (XBUF)
           }
        }

        // SMEM
        Device (SMD0)
        {

            Name (_HID, "QCOM2407")
            Name (_UID, 0)

            Method (_CRS, 0x0, NotSerialized) 
            {

                Name (RBUF, ResourceTemplate ()
                {
                    // Shared memory
                    Memory32Fixed (ReadWrite, 0x86300000, 0x00100000)

                    // Hardware mutexes used to synchronize processors:
                    // HWIO_TCSR_MUTEX_MUTEX_REGn_PHYS(0)
                    Memory32Fixed (ReadWrite, 0x1905000, 0x00020000)

                    // IMEM or TZ_WONCE
                    Memory32Fixed (ReadWrite, 0x0193D000, 0x00000008)

                    // RPM MSG RAM
                    Memory32Fixed (ReadWrite, 0x00060000, 0x00004000)

                    // The rest of the memory resources are those used by SMD
                    // and SMSM to send interrupts

                    // APCS_IPC
                    Memory32Fixed (ReadWrite, 0x0B011008, 0x00000004)

                    // Inbound interrupt from modem:
                    // mss_sw_to_kpss_ipc_irq0 = CsrIrq0 = 57
                    Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {57}

                    // Inbound interrupt from wcnss:
                    // o_wcss_apss_smd_med = WcssAppsSmdMedIrq = 174
                    Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {174}

                    // Inbound SMP2Pinterrupt from modem:
                    Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {59}

                    // Inbound SMP2P interrupt from wcnss:
                    Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {175}

                    // Inbound SMSM interrupt from modem:
                    // mss_sw_to_kpss_ipc_irq1 = CsrIrq1 = 50
                    Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {58}

                    // Inbound SMSM interrupt from WCNSS:
                    // o_wcss_apss_smsm_irq = WcssApssSmsmIrq = 176
                    Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {176}
                })
                
                Return (RBUF)
            }

            // ACPI method to return interrupt descriptor
            Method(INTR, 0x0, NotSerialized) 
            {
                Name(RBUF, Package()
                {
                    // Version
                    0x00000003,
                    // Number of SMD interrupts
                    0x00000002,
                    // Number of SMP2P interrupts
                    0x00000002,
                    // Number of SMSM interrupts
                    0x00000002,

                    // Modem: APCS_IPC(12)
                    // Host = SMEM_MODEM
                    0x00000001,
                    // Physical address
                    0x0B011008,
                    // Value
                    0x00001000,
                    // Reserved
                    0x00000000,

                    // WCNSS: APCS_IPC(17)
                    // Host = SMEM_WCNSS
                    0x00000004,
                    // Physical address
                    0x0B011008,
                    // Value
                    0x00020000,
                    // Reserved
                    0x00000000,

                    // Modem: APCS_IPC(14)
                    // Host = SMEM_MODEM
                    0x00000001,
                    // Physical address
                    0x0B011008,
                    // Value
                    0x00004000,
                    // Reserved
                    0x00000000,

                    // WCNSS: APCS_IPC(18)
                    // Host = SMEM_WCNSS
                    0x00000004,
                    // Physical address
                    0x0B011008,
                    // Value
                    0x00040000,
                    // Reserved
                    0x00000000,

                    // Modem: APCS_IPC(13)
                    // Host = SMSM_MODEM
                    0x00000001,
                    // Physical address
                    0x0B011008,
                    // Value
                    0x00002000,
                    // Reserved
                    0x00000000,

                    // WCNSS (RIVA): APCS_IPC(19)
                    // Host = SMSM_WCNSS
                    0x00000003,
                    // Physical address
                    0x0B011008,
                    // Value
                    0x00080000,
                    // Reserved
                    0x00000000
                })

                Return (RBUF)
            }

        }

	}

}