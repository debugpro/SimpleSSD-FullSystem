# Copyright (C) 2017 CAMELab
#
# This file is part of SimpleSSD.
#
# SimpleSSD is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# SimpleSSD is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.

from m5.SimObject import SimObject
from m5.params import *
from PciDevice import PciDevice


class SATAInterface(PciDevice):
    type = 'SATAInterface'
    cxx_header = "dev/storage/sata_interface.hh"
    SSDConfig = Param.String('./config.cfg', "SimpleSSD Configuration File")

    # NAME                      # START DESC
    # PCIe Header
    # See Intel 9 Series Chipset Family Platform Controller Hub Datasheet
    VendorID = 0x8086           # 00    Intel
    DeviceID = 0x8C80           # 02
    Command = 0x0000            # 04
    Status = 0x02B0             # 06    Capability List
    Revision = 0x00             # 08
    ProgIF = 0x01               # 09
    SubClassCode = 0x06         # 0A    AHCI Controller
    ClassCode = 0x01            # 0B    Mass storage controller
    # CacheLineSize             # 0C    Written by system
    # LatencyTimer              # 0D    Master Latency Timer (ZERO)
    # HeaderType                # 0E    Single Function | Header Layout
    # BIST                      # 0F    Built-in Self Test (ZERO)
    BAR0 = 0x00000001           # 10    Primary controller command block
    BAR1 = 0x00000001           # 14    Primary controller control block
    BAR2 = 0x00000001           # 18    Secondary controller command block
    BAR3 = 0x00000001           # 1C    Secondary controller control block
    BAR4 = 0x00000001           # 20    Legacy Bus Master
    BAR5 = 0x00000000           # 24    AHCI
    # CardbusCIS                # 28    Not supported by NVMe (ZERO)
    SubsystemVendorID = 0x0000  # 2C
    SubsystemID = 0x0000        # 2E
    # ExpansionROM              # 30    Not supported (ZERO)
    CapabilityPtr = 0x80        # 34    First capability pointer
    # Reserved                  # 35
    InterruptLine = 0x0B        # 3C    Interrupt Line
    InterruptPin = 0x02         # 3D    Use INT B
    # MaximumLatency            # 3E    Not supported by NVMe (ZERO)
    # MinimumGrant              # 3F    Not supported by NVMe (ZERO)

    # PMCAP - PCI Power Management Capability
    PMCAPBaseOffset = 0x70      # --    PMCAP capability base
    PMCAPCapId = 0x01           # 70    PMCAP ID
    PMCAPNextCapability = 0xA8  # 71    Next capability pointer
    # 72    Device Specific Initialization (No) | Version (1.2)
    PMCAPCapabilities = 0x0003
    PMCAPCtrlStatus = 0x0008    # 74    No Soft Reset

    # MSICAP - Message Signaled Interrupt Capability
    MSICAPBaseOffset = 0x80     # --    MSICAP capability base
    MSICAPCapId = 0x05          # 80    MSICAP ID
    MSICAPNextCapability = 0x70  # 81    Next capability pointer
    MSICAPMsgCtrl = 0x0000      # 82    MSI Message Control
    # MSICAPMsgAddr             # 84    MSI Message Address
    # MSICAPMsgData             # 88    MSI Message Data

    # SATA Capability
    # See source code for this section
    # A8: 0x12
    # A9: 0x00
    # AA: 0x10
    # AB: 0x00
    # AC: 0x48 (BAR4, 0ffset 0x10)
    # AD: 0x00
    # AE: 0x00
    # AF: 0x00

    BAR0Size = '8B'
    BAR1Size = '4B'
    BAR2Size = '8B'
    BAR3Size = '4B'
    BAR4Size = '32B'
    BAR5Size = '1024B'
