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

#include "dev/storage/sata_interface.hh"

#include "cpu/intr_control.hh"
#include "dev/storage/simplessd/hil/sata/hba.hh"
#include "dev/storage/simplessd/util/algorithm.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"

#undef panic
#undef warn
#undef info

#include "dev/storage/simplessd/log/log.hh"
#include "dev/storage/simplessd/log/trace.hh"
#include "dev/storage/simplessd/util/interface.hh"

SATAInterface::SATAInterface(Params *p)
    : PciDevice(p), configPath(p->SSDConfig), lastReadDMAEndAt(0),
      lastWriteDMAEndAt(0), IS(0), ISold(0), mode(INTERRUPT_PIN) {
  SimpleSSD::Logger::initLogSystem(std::cout, std::cerr,
                                   []() -> uint64_t { return curTick(); });

  if (!conf.init(configPath)) {
    SimpleSSD::Logger::panic("Failed to read SimpleSSD configuration");
  }

  // Set SATACR
  satacrOffset = 0xA8;
  satacrSize = 8;
  satacr.cid = 0x12;      // SATA Capability
  satacr.next = 0x00;     // No next capability
  satacr.revision = 0x10; // v1.0
  satacr.reserved = 0x00;
  satacr.offset = 0x00000048; // BAR4, Offset 0x10

  pHBA = new SimpleSSD::HIL::SATA::HBA(this, &conf);
}

SATAInterface::~SATAInterface() { delete pHBA; }

Tick SATAInterface::readConfig(PacketPtr pkt) {
  int offset = pkt->getAddr() & PCI_CONFIG_SIZE;
  int size = pkt->getSize();

  if (offset < PCI_DEVICE_SPECIFIC) {
    return PciDevice::readConfig(pkt);
  } else {
    // Read on PCI capabilities
    uint32_t val = 0;
    for (int i = 0; i < size; i++) {
      if (offset + i >= PMCAP_BASE && offset + i < PMCAP_BASE + PMCAP_SIZE) {
        val |= (uint32_t)pmcap.data[offset + i - PMCAP_BASE] << (i * 8);
      } else if (offset + i >= MSICAP_BASE &&
                 offset + i < MSICAP_BASE + MSICAP_SIZE) {
        val |= (uint32_t)msicap.data[offset + i - MSICAP_BASE] << (i * 8);
      } else if (offset + i >= satacrOffset &&
                 offset + i < satacrOffset + satacrSize) {
        val |= (uint32_t)satacr.data[offset + i - satacrOffset] << (i * 8);
      } else {
        SimpleSSD::Logger::warn(
            "sata_interface: Invalid PCI config read offset: %#x", offset);
      }
    }

    switch (size) {
    case sizeof(uint8_t):
      pkt->set<uint8_t>(val);
      break;
    case sizeof(uint16_t):
      pkt->set<uint16_t>(val);
      break;
    case sizeof(uint32_t):
      pkt->set<uint32_t>(val);
      break;
    default:
      SimpleSSD::Logger::warn(
          "sata_interface: Invalid PCI config read size: %d", size);
      break;
    }

    pkt->makeAtomicResponse();
  }

  return configDelay;
}

Tick SATAInterface::writeConfig(PacketPtr pkt) {
  int offset = pkt->getAddr() & PCI_CONFIG_SIZE;
  int size = pkt->getSize();
  uint32_t val = 0;

  if (offset < PCI_DEVICE_SPECIFIC) {
    PciDevice::writeConfig(pkt);

    switch (offset) {
    case PCI0_BASE_ADDR0:
      if (BARAddrs[0] != 0) {
        priCommandBlock = BARAddrs[0];
      }

      break;
    case PCI0_BASE_ADDR1:
      if (BARAddrs[1] != 0) {
        priControlBlock = BARAddrs[1];
      }

      break;
    case PCI0_BASE_ADDR2:
      if (BARAddrs[2] != 0) {
        secCommandBlock = BARAddrs[2];
      }

      break;
    case PCI0_BASE_ADDR3:
      if (BARAddrs[3] != 0) {
        secControlBlock = BARAddrs[3];
      }

      break;
    case PCI0_BASE_ADDR4:
      if (BARAddrs[4] != 0) {
        legacyBusMaster = BARAddrs[4];
        indexDataPair = BARAddrs[4] + 16;
      }

      break;
    case PCI0_BASE_ADDR5:
      if (BARAddrs[5] != 0) {
        AHCIRegisters = BARAddrs[5];
      }

      break;
    }
  } else {
    // Write on PCI capabilities
    if (offset == PMCAP_BASE + 4 &&
        size == sizeof(uint16_t)) { // PMCAP Control Status
      val = pkt->get<uint16_t>();

      if (val & 0x8000) {
        pmcap.pmcs &= 0x7F00; // Clear PMES
      }
      pmcap.pmcs &= ~0x1F03;
      pmcap.pmcs |= (val & 0x1F03);
    } else if (offset == MSICAP_BASE + 2 &&
               size == sizeof(uint16_t)) { // MSICAP Message Control
      val = pkt->get<uint16_t>();

      mode = (val & 0x0001) ? INTERRUPT_MSI : INTERRUPT_PIN;

      msicap.mc &= ~0x0071;
      msicap.mc |= (val & 0x0071);

      vectors = (uint16_t)powf(2, (msicap.mc & 0x0070) >> 4);

      SimpleSSD::Logger::debugprint(
          SimpleSSD::Logger::LOG_HIL_SATA, "INTR    | MSI %s | %d vectors",
          mode == INTERRUPT_PIN ? "disabled" : "enabled", vectors);
    } else if (offset == MSICAP_BASE + 4 &&
               size == sizeof(uint32_t)) { // MSICAP Message Address
      msicap.ma = pkt->get<uint32_t>() & 0xFFFFFFFC;
    } else if (offset == MSICAP_BASE + 8 &&
               size >= sizeof(uint16_t)) { // MSICAP Message Data
      msicap.md = pkt->get<uint16_t>();
    } else {
      SimpleSSD::Logger::panic(
          "nvme_interface: Invalid PCI config write offset: %#x size: %d",
          offset, size);
    }

    pkt->makeAtomicResponse();
  }

  return configDelay;
}

Tick SATAInterface::read(PacketPtr pkt) {
  Addr addr = pkt->getAddr();
  int size = pkt->getSize();
  uint8_t *buffer = pkt->getPtr<uint8_t>();
  Tick begin = curTick();
  Tick end = curTick();

  if (addr >= priCommandBlock && addr + size <= priCommandBlock + 8) {
    // TODO
  } else if (addr >= priControlBlock && addr + size <= priControlBlock + 4) {
    // TODO
  } else if (addr >= secCommandBlock && addr + size <= secCommandBlock + 8) {
    // TODO
  } else if (addr >= secControlBlock && addr + size <= secControlBlock + 4) {
    // TODO
  } else if (addr >= legacyBusMaster && addr + size <= legacyBusMaster + 16) {
    // TODO
  } else if (addr >= indexDataPair && addr + size <= indexDataPair + 16) {
    // TODO
  } else if (addr >= AHCIRegisters && addr + size <= AHCIRegisters + 1024) {
    // TODO
  } else {
    SimpleSSD::Logger::panic("sata_interface: Invalid address access!");
  }

  pkt->makeAtomicResponse();

  return end - begin;
}

Tick SATAInterface::write(PacketPtr pkt) {
  Addr addr = pkt->getAddr();
  int size = pkt->getSize();
  uint8_t *buffer = pkt->getPtr<uint8_t>();
  Tick begin = curTick();
  Tick end = curTick();

  // TODO

  pkt->makeAtomicResponse();

  return end - begin;
}

void SATAInterface::writeInterrupt(Addr addr, size_t size, uint8_t *data) {
  Addr dmaAddr = hostInterface.dmaAddr(addr);

  DmaDevice::dmaWrite(dmaAddr, size, NULL, data);
}

uint64_t SATAInterface::dmaRead(uint64_t addr, uint64_t size, uint8_t *buffer,
                                uint64_t &tick) {
  uint64_t latency = SimpleSSD::PCIExpress::calculateDelay(
      SimpleSSD::PCIExpress::PCIE_2_X, 4, size);
  uint64_t delay = 0;

  // DMA Scheduling
  if (tick == 0) {
    tick = lastReadDMAEndAt;
  }

  if (lastReadDMAEndAt <= tick) {
    lastReadDMAEndAt = tick + latency;
  } else {
    delay = lastReadDMAEndAt - tick;
    lastReadDMAEndAt += latency;
  }

  if (buffer) {
    DmaDevice::dmaRead(pciToDma(addr), size, nullptr, buffer);
  }

  delay += tick;
  tick = delay + latency;

  return delay;
}

uint64_t SATAInterface::dmaWrite(uint64_t addr, uint64_t size, uint8_t *buffer,
                                 uint64_t &tick) {
  uint64_t latency = SimpleSSD::PCIExpress::calculateDelay(
      SimpleSSD::PCIExpress::PCIE_2_X, 4, size);
  uint64_t delay = 0;

  // DMA Scheduling
  if (tick == 0) {
    tick = lastWriteDMAEndAt;
  }

  if (lastWriteDMAEndAt <= tick) {
    lastWriteDMAEndAt = tick + latency;
  } else {
    delay = lastWriteDMAEndAt - tick;
    lastWriteDMAEndAt += latency;
  }

  if (buffer) {
    DmaDevice::dmaWrite(pciToDma(addr), size, nullptr, buffer);
  }

  delay += tick;
  tick = delay + latency;

  return delay;
}

void SATAInterface::updateInterrupt(uint16_t iv, bool post) {
  switch (mode) {
  case INTERRUPT_PIN:
    if (post) {
      IS |= (1 << iv);
    } else {
      IS &= ~(1 << iv);
    }

    if (IS == ISold) {
      break;
    }

    if (IS == 0) {
      intrClear();

      SimpleSSD::Logger::debugprint(SimpleSSD::Logger::LOG_HIL_SATA,
                                    "INTR    | Pin Interrupt Clear");
    } else {
      intrPost();

      SimpleSSD::Logger::debugprint(SimpleSSD::Logger::LOG_HIL_SATA,
                                    "INTR    | Pin Interrupt Post");
    }

    ISold = IS;

    break;
  case INTERRUPT_MSI: {
    uint32_t data = msicap.md;

    if (post) {
      if (vectors > 1) { // Multiple MSI
        data &= ~(vectors - 1);
        data |= iv & (vectors - 1);
      }

      writeInterrupt(msicap.ma, sizeof(uint32_t), (uint8_t *)&data);

      SimpleSSD::Logger::debugprint(SimpleSSD::Logger::LOG_HIL_SATA,
                                    "INTR    | MSI sent | vector %d", iv);
    }
  }

  break;
  default:
    break;
  }
}

void SATAInterface::serialize(CheckpointOut &cp) const {
  // FIXME: Not implemented
}

void SATAInterface::unserialize(CheckpointIn &cp) {
  // FIXME: Not implemented
}

SATAInterface *SATAInterfaceParams::create() { return new SATAInterface(this); }
