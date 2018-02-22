//
// Copyright (c) 2011,2014-2015 Qualcomm Technologies Inc. All rights reserved.
//

// 
// Secure Channel Manager (SCM) Driver
//
Device (SCM0)
{
    Name (_DEP, Package (One)  // _DEP: Dependencies
    {
        \_SB.PEP0
    })
    Name (_HID, "QCOM2402")  // _HID: Hardware ID
    Name (_UID, Zero)  // _UID: Unique ID
}

Device (TREE)
{
    Name (_HID, "QCOM24BA")  // _HID: Hardware ID
    Name (_UID, Zero)  // _UID: Unique ID
}
