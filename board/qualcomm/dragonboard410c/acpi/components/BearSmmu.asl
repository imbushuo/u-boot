//
// SMMU Driver instance for aTCU
//

Device (ATCU)
{

    Name (_HID, "QCOM2400")
    Name (_UID, 0)
    Name (_DEP, Package ()
    {
        \_SB_.GTCU
    })

    Method (_CRS, 0x0, NotSerialized)
    {
        Return (ResourceTemplate ()
        {
            // a-TCU register address space
            Memory32Fixed (ReadWrite, 0x1E00000, 0x40000)
            
            //a-TCU: there is only one interrupt(aggregated for all CBs) HLOS needs to handle, TBU perf interrupts will be added later

            Interrupt (ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {102}
        })
    }

    // aTCU GUID
    Method (GUID)
    {
        Return (ToUUID ("36079AE4-78E8-452D-AF50-0CFF78B2F1CA"))
    }

    // GUID and ref string for aTCU masters. This GUID is required
    // to return the corresponding CBs to the master.
    Method (CLID)
    {
        Return (Package ()
        {
            Package()
            {
                ToUUID ("C461B828-B8AD-4113-939A-8934272F9102"), //VENUS
                ToUUID ("C00DE5A5-E6E0-4DD7-B8C3-2B71AB6FCA15"), //VFE
                ToUUID ("DE2EAA3D-0FA5-45E9-AC9D-A494C6C04D7C"), //MDP
                ToUUID ("84A989BD-145D-4985-83BD-1A80829B5030") //IJPEG
            }    
        })
    }


    // Number of total CBs in this TCU
    Method (CBNO)
    {
        Name (BUFF, Buffer ()
        {
            0x20 
        })
        Return (BUFF)
    }

    // Number of CBs for each master and offset
    
    Method (CBMA)
    {
        Return (Buffer ()
        {
            0x4,               //Venus
            0x2,               //VFE
            0x2,               //MDP
            0x1,                //JPEG
        })
    }

    // context information for  masters
    Method (CNTX)
    {
        Return (Package ()
        {
            // VENUS
            Package()
            {
                0x05,0x00,               // Context index 5: Non Secure
                0x13,0x01,               // Context index 19: CPZ
                0x14,0x01,               // Context index 20: CPZ
                0x15,0x01                // Context index 21: CPZ
            
            },
            // VFE
            Package()
            {
                0x03,0x00,               // Context index 3: 0 - Non Secure, 1 - CPZ
                0x06,0x00                // Context index 6: 0 - Non Secure, 1 - CPZ
                
            },
            // MDP
            Package()
            {
                0x04,0x00,               // Context index 4: Non Secure
                0x12,0x01                // Context index 18: CPZ
            },
            // JPEG
            Package()
            {
                0x02,0x00               // Context index 2: Non Secure
            }
        })
    }

    // This OFFI method returns a buffer that describes the layout of
    // the SMMU register space. Each entry corresponds to page number
    // from the base address (from _CRS) for the register group
    // specified in the comments to the right.

    Method (OFFI)
    {
        Return (Buffer ()
        {
            0x00,                // Global 0 page offset from Base Address
            0x01,                // Global 1 page offset from Base Address
            0x02,                // Implementation defined page offset from Base Address
            0x03,                // Perf page offset from base address
            0x04,                // SSD page offset from base address
            0x20                 // CB page offset from base address
        })
    }
    
    //stream ID -> CB mapping info for pre-silicon/bring up purpose


    // The S2CB method returns a package that describes the stream to
    // context bank mapping that allows the SMMU driver to initialize
    // the SMMU so that the core's transaction stream will be mapped to
    // the correct context bank.

    Method (S2CB)
    {
        // Data from MMU-Config-MDP.xlsx, MMU-Strm-Map tab.

        // The length of the package indicates the number of stream to
        // context bank entires to program.
        // at this point s2cb entries are not real ones

        Return (Package ()
        {
            Package ()  //MDP
            {
                0x0,    // Stream mapping table entry
                0x0,    // Stream ID
                0x7c00, // Stream Mask
                0x2     // Context bank
            },
            Package ()  //MDP
            {
                0x1,    // Stream mapping table entry
                0x1,    // Stream ID
                0x7C00, // Stream Mask
                0x6     // Context bank (Not specified in the document, need to confirm).
            }
        })
    }
    
    // Method to indicate whether TZ is availble or not
    Method (ISTZ)
    {
        Return (Buffer ()
        {
            0x1                // 0: not availbale 1: available
        })
    }

    Method (VRTO)
    {
        Return (Buffer ()
        {
            0x1                // 0: not availbale 1: available
        })
    }
}

//
// SMMU Driver instance for gTCU
//

Device (GTCU)
{

    Name (_HID, "QCOM2400")
    Name (_UID, 1)

    Method (_CRS, 0x0, NotSerialized)
    {
        Return (ResourceTemplate ()
        {
            // g-TCU register address space
            Memory32Fixed (ReadWrite, 0x1F00000, 0x10000)

            // g-TCU: there are four interrupts(one for each CB) HLOS needs to handle, TBU perf interrupts will be added later
            Interrupt (ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {273}
            Interrupt (ResourceConsumer, Edge, ActiveHigh, Exclusive, , , ) {274}
        })
    }

    // gTCU GUID
    Method (GUID)
    {
        Return (ToUUID ("53191EB7-5909-4972-8F7C-7E47B450BE94"))
    }

    // GUID for aTCU clients . This GUID is required
    // to return the corresponding CBs to the master.
    Method (CLID)
    {
        Return (Package ()
        {
                Package()
    {
        ToUUID ("9833C712-3292-4FFB-B0F4-2BD20E1F7F66") //GPU
    }   
            
        })
    }

    // Number of total CBs in this TCU
    Method (CBNO)
    {
        Name (BUFF, Buffer ()
        {
            0x4 
        })
        Return (BUFF)
    }

    // Number of CBs for each master
    
    Method (CBMA)
    {
        Return (Buffer ()
        {
            0x2
        
        })
    }

    // the below data is only to show acpi structure, CB information is not updated

    // context information for  masters
    Method (CNTX)
    {
        Return (Package ()
        {
            Package()
            {
                0x01,0x00,               //Context index 0: 0 - Non Secure, 1 - CPZ
                0x02,0x00               //Context index 0: 0 - Non Secure, 1 - CPZ
                
            }
        })
    }
    
    
    // This OFFI method returns a buffer that describes the layout of
    // the SMMU register space. Each entry corresponds to page number
    // from the base address (from _CRS) for the register group
    // specified in the comments to the right.

    Method (OFFI)
    {
        Return (Buffer ()
        {
            0x00,                // Global 0 page offset from Base Address
            0x01,                // Global 1 page offset from Base Address
            0x02,                // Implementation defined page offset from Base Address
            0x03,                // Perf page offset from base address
            0x04,                // SSD page offset from base address
            0x08                 // CB page offset from base address
        })
    }
    
    // Stream ID -> CB mapping info for pre-silicon/bring up purpose


    // The S2CB method returns a package that describes the stream to
    // context bank mapping that allows the SMMU driver to initialize
    // the SMMU so that the core's transaction stream will be mapped to
    // the correct context bank.

    Method (S2CB)
    {
        // Data from MMU-Config-Gfx.xlsx, MMU-Strm-Map tab.

        // The length of the package indicates the number of stream to
        // context bank entires to program.

        Return (Package ()
        {
            Package ()
            {
                0x00,   // Stream mapping table entry
                0x0000, // Stream ID
                0x7C00, // Stream Mask
                0x00    // Context bank (Not specified in the document, need to confirm).
            },
            Package ()
            {
                0x1,   // Stream mapping table entry
                0x1, // Stream ID
                0x7C00, // Stream Mask
                0x1    // Context bank (Not specified in the document, need to confirm).
            }
        })
    }

    // Method to indicate whether TZ is availble or not
    Method (ISTZ)
    {
        Return (Buffer ()
        {
            0x1                //0: not availbale 1: available
        })
    }

    Method (VRTO)
    {
        Return (Buffer ()
        {
            0x1                //0: not availbale 1: available
        })
    }           
    
}