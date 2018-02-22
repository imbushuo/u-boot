/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20180105 (32-bit version)
 * Copyright (c) 2000 - 2018 Intel Corporation
 * 
 * Disassembly of madt.acp, Tue Feb 20 10:22:31 2018
 *
 * ACPI Data Table [APIC]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "APIC"    [Multiple APIC Description Table (MADT)]
[004h 0004   4]                 Table Length : 000000E4
[008h 0008   1]                     Revision : 03
[009h 0009   1]                     Checksum : 00
[00Ah 0010   6]                       Oem ID : "QCOM  "
[010h 0016   8]                 Oem Table ID : "QCOMUBOT"
[018h 0024   4]                 Oem Revision : 00000001
[01Ch 0028   4]              Asl Compiler ID : "QCOM"
[020h 0032   4]        Asl Compiler Revision : 00000001

[024h 0036   4]           Local Apic Address : 0B002000
[028h 0040   4]        Flags (decoded below) : 00000000
                         PC-AT Compatibility : 0

/* Our implementation supports PSCI, so no Microsoft MP-Protocol is required */

[02Ch 0044   1]                Subtable Type : 0B [Generic Interrupt Controller]
[02Dh 0045   1]                       Length : 28
[02Eh 0046   2]                     Reserved : 0000
[030h 0048   4]         CPU Interface Number : 00000000
[034h 0052   4]                Processor UID : 00000000
[038h 0056   4]        Flags (decoded below) : 00000001
                           Processor Enabled : 1
          Performance Interrupt Trigger Mode : 0
          Virtual GIC Interrupt Trigger Mode : 0
[03Ch 0060   4]     Parking Protocol Version : 00000000
[040h 0064   4]        Performance Interrupt : 00000017
[044h 0068   8]               Parked Address : FFFFFFFFFFFFFFFF
[04Ch 0076   8]                 Base Address : 0000000000000000

[054h 0084   1]                Subtable Type : 0B [Generic Interrupt Controller]
[055h 0085   1]                       Length : 28
[056h 0086   2]                     Reserved : 0000
[058h 0088   4]         CPU Interface Number : 00000001
[05Ch 0092   4]                Processor UID : 00000001
[060h 0096   4]        Flags (decoded below) : 00000001
                           Processor Enabled : 1
          Performance Interrupt Trigger Mode : 0
          Virtual GIC Interrupt Trigger Mode : 0
[064h 0100   4]     Parking Protocol Version : 00000000
[068h 0104   4]        Performance Interrupt : 00000017
[06Ch 0108   8]               Parked Address : FFFFFFFFFFFFFFFF
[074h 0116   8]                 Base Address : 0000000000000000

[07Ch 0124   1]                Subtable Type : 0B [Generic Interrupt Controller]
[07Dh 0125   1]                       Length : 28
[07Eh 0126   2]                     Reserved : 0000
[080h 0128   4]         CPU Interface Number : 00000002
[084h 0132   4]                Processor UID : 00000002
[088h 0136   4]        Flags (decoded below) : 00000001
                           Processor Enabled : 1
          Performance Interrupt Trigger Mode : 0
          Virtual GIC Interrupt Trigger Mode : 0
[08Ch 0140   4]     Parking Protocol Version : 00000000
[090h 0144   4]        Performance Interrupt : 00000017
[094h 0148   8]               Parked Address : FFFFFFFFFFFFFFFF
[09Ch 0156   8]                 Base Address : 0000000000000000

[0A4h 0164   1]                Subtable Type : 0B [Generic Interrupt Controller]
[0A5h 0165   1]                       Length : 28
[0A6h 0166   2]                     Reserved : 0000
[0A8h 0168   4]         CPU Interface Number : 00000003
[0ACh 0172   4]                Processor UID : 00000003
[0B0h 0176   4]        Flags (decoded below) : 00000001
                           Processor Enabled : 1
          Performance Interrupt Trigger Mode : 0
          Virtual GIC Interrupt Trigger Mode : 0
[0B4h 0180   4]     Parking Protocol Version : 00000000
[0B8h 0184   4]        Performance Interrupt : 00000017
[0BCh 0188   8]               Parked Address : FFFFFFFFFFFFFFFF
[0C4h 0196   8]                 Base Address : 0000000000000000

[0CCh 0204   1]                Subtable Type : 0C [Generic Interrupt Distributor]
[0CDh 0205   1]                       Length : 18
[0CEh 0206   2]                     Reserved : 0000
[0D0h 0208   4]        Local GIC Hardware ID : 00000000
[0D4h 0212   8]                 Base Address : 000000000B000000
[0DCh 0220   4]               Interrupt Base : 00000000
[0E0h 0224   1]                      Version : 00
[0E1h 0225   3]                     Reserved : 000000
