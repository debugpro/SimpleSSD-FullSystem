/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __DEV_STORAGE_NVME_INTERFACE_HH__
#define __DEV_STORAGE_NVME_INTERFACE_HH__

#include <cinttypes>

#include "dev/io_device.hh"
#include "dev/pci/device.hh"
#include "dev/storage/simplessd/hil/sata/interface.hh"
#include "dev/storage/simplessd/util/config.hh"
#include "params/SATAInterface.hh"

typedef enum _INTERRUPT_MODE {
  INTERRUPT_PIN,
  INTERRUPT_MSI,
  INTERRUPT_MSIX
} INTERRUPT_MODE;

union SATACR {
  uint8_t data[8];
  struct {
    uint8_t cid;
    uint8_t next;
    uint8_t revision;
    uint8_t reserved;
    uint32_t offset;
  };
};

class SATAInterface : public PciDevice, public SimpleSSD::HIL::SATA::Interface {
private:
  std::string configPath;
  SimpleSSD::ConfigReader conf;

  // BAR addresses
  uint32_t priCommandBlock;
  uint32_t priControlBlock;
  uint32_t secCommandBlock;
  uint32_t secControlBlock;
  uint32_t legacyBusMaster;
  uint32_t indexDataPair;
  uint32_t AHCIRegisters;

  // DMA scheduling
  uint64_t lastReadDMAEndAt;
  uint64_t lastWriteDMAEndAt;

  // SATA capability
  uint32_t satacrOffset;
  uint32_t satacrSize;
  SATACR satacr;

  /* Interrupt logics */
  // Pin based
  uint32_t IS;
  uint32_t ISold;

  // MSI/MSI-X
  uint16_t vectors;

  // Current Interrupt Mode
  INTERRUPT_MODE mode;

  void writeInterrupt(Addr, size_t, uint8_t *);

public:
  typedef SATAInterfaceParams Params;
  const Params *params() const { return (const Params *)_params; }
  SATAInterface(Params *p);
  ~SATAInterface();

  Tick writeConfig(PacketPtr pkt) override;
  Tick readConfig(PacketPtr pkt) override;

  Tick read(PacketPtr pkt) override;
  Tick write(PacketPtr pkt) override;

  void serialize(CheckpointOut &cp) const override;
  void unserialize(CheckpointIn &cp) override;

  // Interface <-> Controller
  uint64_t dmaRead(uint64_t, uint64_t, uint8_t *, uint64_t &) override;
  uint64_t dmaWrite(uint64_t, uint64_t, uint8_t *, uint64_t &) override;
  void updateInterrupt(uint16_t, bool) override;
};

#endif
