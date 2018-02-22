//
// Copyright (c) 2011-2012, Qualcomm Technologies Inc. All rights reserved.
//
// This file contains the Power Management IC (PMIC)
// ACPI device definitions, configuration and look-up tables.
// 

// PMIC
Device (PMIC)
{

    Name (_DEP, Package(0x1)
    {
        \_SB_.SPMI
    })
    Name (_HID, "QCOM2455")
    
    Method (PMCF) 
    {
        // PMIC Info
        Name (CFG0,
        Package()
        {
            1,  // Number of PMICs, must match the number of info packages
            Package()
            {
                0,
                1,
            },
        })
        Return (CFG0)
    }

}

// PMIC GPIO
Device (PM01)
{

    Name (_DEP, Package(0x1)
    {
        \_SB_.PMIC
    })
    
    Name (_HID, "QCOM2458")
    Name (_UID, 1)

    Method (_CRS, 0x0, NotSerialized) 
    {
        Name (RBUF, ResourceTemplate ()
        {
            //
            // QGIC Interrupt Resource
            //
            // Register for SPMI interrupt 222
            //
            Interrupt(ResourceConsumer, Level, ActiveHigh, Shared, , ,) {222}
        })
        Return (RBUF)
    }

    
    Method (PMIO) 
    {
        Name (CFG0, 
        Package()
        {     
            // Generic controller Info
            0,  	// PMIC index for PM8916,  
            0,  	// First Slave ID for PMIC 8916

            // Details about IRQ
            4160,  	// NumIRQPins = 256*2 Peripheral* 8 Interrupt per peripheral + 64 virtual interrupts
            64,  	// NumIRQPerBank

            // Details about physical GPIO pins				
            4,  	// NumGPIOPins 
            0xC0, 	// GPIOIRQStart

            // Details about physical MPP pins
            4,     	// NumMPPPins  
            0xA0,   // MPPIRQStart
        
            //Details about SPMI IRQ controller
            0x02000000,		// Block base address
            0x01A00000,		// Block size
            0				// Owner ID: 0 = Apps Proc			
        })
        Return (CFG0)
    }
                
    // _DSM method to mark PM01's ActiveHigh interrupts
    Method(_DSM, 0x4, NotSerialized) 
    {
        // DSM UUID
        switch(ToBuffer(Arg0))
        {
            // ACPI DSM UUID for GPIO
            case(ToUUID("4F248F40-D5E2-499F-834C-27758EA1CD3F"))
            {
                // DSM Function
                switch(ToInteger(Arg2))
                {
                    //
                    // Function 0: Return supported functions, based on revision
                    //              
                    case(0)
                    {
                    
                        // revision 0: function 0 & 1 are supported. 
                        return (Buffer() {0x3})
                    }
                                    
                    //
                    // Function 1: For emulated ActiveBoth controllers, returns
                    //             a package of controller-relative pin numbers.
                    //             Each corresponding pin will have an initial
                    //             polarity of ActiveHigh. 
                    //       
                    case(1)
                    {      
                        // Marks pins KPDPWR_ON, RESIN_ON to be ActiveHigh.
                        Return (Package() {64,65,0x1001,0x1002})
                    }    

                    default
                    {
                        // Functions 2+: not supported
                    }
                                        
                }
            }
                        
            default
            {
                // No other GUIDs supported
                Return(Buffer(One) { 0x00 })
            }
        }
    }  //end of _DSM 

}

// PMIC Apps Driver
Device (PMAP) 
{
    Name(_DEP, Package(0x3) 
    {
        \_SB_.PMIC,
        \_SB.ABD,
        \_SB.SCM0
    })
		
    Name (_HID, "QCOM2457")
			
    // PMAP is dependent on ABD for operation region access
			
    // Get pseudo SPB controller port which is used to handle the ACPI operation region access
    Method(GEPT)
    {
        Name(BUFF, Buffer(4){})
        CreateByteField(BUFF, 0x00, STAT)
        CreateWordField(BUFF, 0x02, DATA)
        Store(0x2, DATA)
        Return(DATA)
    }
			
    Method (_CRS, 0x0, NotSerialized)
    {
        Name (RBUF, ResourceTemplate ()
        {
            // Fake interrupt
            GpioInt(Edge, ActiveHigh, SharedAndWake, PullUp, 6200, "\\_SB.PM01", , , ,) {0x1000}
        })
        Return (RBUF)
    }
}

//
// PMIC ACPI RTC Interface
//
// For details see MSFT Document:
//
// "ACPI Design Guide for Windows on SoC Platforms" June 10, 2011, Version 0.92
// 
// CMND Values come from PmicIAbd.h pm_ioctl_abd_invoke_type
//
// typedef enum
// {
// 0 - PM_IOCTL_ABD_GET_TIME_AND_ALARM_CAPS,   // Handled in ACPI - Get the capabilities of the time and alarm device
// 1 - PM_IOCTL_ABD_GET_TIME,                  // Get the Real time
// 2 - PM_IOCTL_ABD_SET_TIME,                  // Set the Real time
// 3 - PM_IOCTL_ABD_GET_WAKE_STATUS,           // Get Wake status
// 4 - PM_IOCTL_ABD_CLEAR_WAKE_STATUS,         // Clear Wake Status
// 5 - PM_IOCTL_ABD_SET_EXP_TIMER_WAKE_POLICY, // Sets expired timer wake policy for the specified timer. 
// 6 - PM_IOCTL_ABD_SET_TIMER_VALUE,           // Sets the value in the specified timer.
// 7 - PM_IOCTL_ABD_GET_EXP_TIMER_WAKE_POLICY, // Returns the current expired timer policy setting of the specified timer.
// 8 - PM_IOCTL_ABD_GET_REMAINING_TIME_FOR_TIMER // Returns the remaining time of the specified timer. 
// } pm_ioctl_abd_invoke_type;
//

Device (PRTC) 
{
    Name (_HID, "ACPI000E")

    // PRTC is dependent on PMAP which implements the RTC Functions
    Name(_DEP, Package() 
    {
        \_SB.PMAP
    })       
			
    // Get the capabilities of the time and alarm device
    Method(_GCP) 
    {
        Return (0x05) //Bit 2 set indicating Get Set Supported
    }
			
    Field(\_SB.ABD.ROP1, BufferAcc, NoLock, Preserve)
    {
        Connection(I2CSerialBus( 0x0002,,0x0,, "\\_SB.ABD",,,,)),
        AccessAs(BufferAcc, AttribRawBytes(24)),
        FLD0,192
    }
			
    // Get the Real time
    Method(_GRT)
    {
        Name(BUFF, Buffer(26){})          // 18 bytes STAT(1), SIZE(1), Time(16)
        CreateField(BUFF, 16, 128, TME1)  // Create the TIME Field - For the time
        CreateField(BUFF, 144, 32, ACT1)  // Create the AC TIMER Field
        CreateField(BUFF, 176, 32, ACW1)  // Create the AC Wake Alarm Status Field
        Store(FLD0, BUFF)
        Return(TME1)
    }

    // Get the AC TIMER Field
    Method(_TIV)
    {
        Name(BUFF, Buffer(26){})          // 18 bytes STAT(1), SIZE(1), Time(16)
        CreateField(BUFF, 16, 128, TME1)  // Create the TIME Field - For the time
        CreateField(BUFF, 144, 32, ACT1)  // Create the AC TIMER Field
        CreateField(BUFF, 176, 32, ACW1)  // Create the AC Wake Alarm Status Field
        Store(FLD0, BUFF)
        Return(ACT1)
    }

    // Get the AC TIMER Wake Status
    Method(_GWS) 
    {
        Name(BUFF, Buffer(26){})          // 18 bytes STAT(1), SIZE(1), Time(16)
        CreateField(BUFF, 16, 128, TME1)  // Create the TIME Field - For the time
        CreateField(BUFF, 144, 32, ACT1)  // Create the AC TIMER Field
        CreateField(BUFF, 176, 32, ACW1)  // Create the AC Wake Alarm Status Field
        Store(FLD0, BUFF)
        Return(ACW1)
    }
			
    // Set alarm timer value
    Method(_STV, 2) 
    {
        If(LEqual(Arg0,0x00)) 
        {
            Name(BUFF, Buffer(50){})         // 18 bytes STAT(1), SIZE(1), Time(16)
            CreateByteField(BUFF, 0x0, STAT) // Create the STAT Field
            CreateField(BUFF, 16, 128, TME1)  // Create the TIME Field - For the time
            CreateField(BUFF, 144, 32, ACT1)  // Create the AC TIMER Field
            CreateField(BUFF, 176, 32, ACW1)  // Create the AC Wake Alarm Status Field
            Store(Arg1, ACT1)
            Store(0x0, TME1)
            Store(0x0, ACW1)
            Store(Store(BUFF, FLD0),BUFF)      // Write the transaction to the Psuedo I2C Port
        
            // Return the status
            If(LNotEqual(STAT,0x00)) 
            {
                Return(1) // Call to OpRegion failed
            }

            Return(0) //success
        }

        Return(1)
    }

    // Set the Real time
    Method(_SRT, 1) 
    {
        Name(BUFF, Buffer(50){})         // 18 bytes STAT(1), SIZE(1), Time(16)
        CreateByteField(BUFF, 0x0, STAT) // Create the STAT Field
        CreateField(BUFF, 16, 128, TME1)  // Create the TIME Field - For the time
        CreateField(BUFF, 144, 32, ACT1)  // Create the AC TIMER Field
        CreateField(BUFF, 176, 32, ACW1)  // Create the AC Wake Alarm Status Field
        Store(0x0, ACT1)
        Store(Arg0, TME1)
        Store(0x0, ACW1)
        Store(Store(BUFF, FLD0),BUFF)      // Write the transaction to the Psuedo I2C Port
        
        // Return the status
        If(LNotEqual(STAT,0x00)) 
        {
            Return(1) // Call to OpRegion failed
        }

        Return(0) // Success
    }
			
    // Clear wake alarm status
    Method(_CWS, 1) 
    {
        Name(BUFF, Buffer(50){})         // 18 bytes STAT(1), SIZE(1), Time(16)
        CreateByteField(BUFF, 0x0, STAT) // Create the STAT Field
        CreateField(BUFF, 16, 128, TME1)  // Create the TIME Field - For the time
        CreateField(BUFF, 144, 32, ACT1)  // Create the AC TIMER Field
        CreateField(BUFF, 176, 32, ACW1)  // Create the AC Wake Alarm Status Field
        Store(0x0, ACT1)
        Store(0x0, TME1)
        Store(Arg0, ACW1)
        Store(Store(BUFF, FLD0),BUFF)      // Write the transaction to the Psuedo I2C Port
        
        // Return the status
        If(LNotEqual(STAT,0x00)) 
        {
            Return(1) // Call to OpRegion failed
        }

        Return(0) //success
    }

}
