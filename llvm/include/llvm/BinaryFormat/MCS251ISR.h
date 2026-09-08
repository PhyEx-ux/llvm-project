//===- llvm/BinaryFormat/MCS251ISR.h - MCS251 ISR protocol ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Shared, frozen constants for the MCS-251 interrupt campaign (ISR-TASK-
// BREAKDOWN.md, interface freeze sections A3/A4/A5). This header is the
// single registration point for:
//   - the `.mcs251.isr` object metadata section layout,
//   - the complete 52-slot interrupt topology with page references,
//   - the vector-area formulas and the frozen default (fail-stop) entry.
//
// Names, numbers, byte layout, slot classification and semantics are frozen.
// Changes may only be made via the design owner through PM arbitration.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_MCS251ISR_H
#define LLVM_BINARYFORMAT_MCS251ISR_H

#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace llvm {
namespace MCS251ISR {

//===----------------------------------------------------------------------===//
// A3.2 Metadata section: .mcs251.isr
//===----------------------------------------------------------------------===//

inline constexpr StringRef MetaSectionName = ".mcs251.isr";
inline constexpr uint64_t MetaSectionAlignment = 4;

/// protocol_version of the first frozen revision.
inline constexpr uint16_t ProtocolVersion = 1;
/// Fixed record size in bytes; the section must be a whole number of
/// records (no section header, no trailing padding).
inline constexpr uint16_t RecordSize = 24;
/// required_caps is frozen to 0001 for this revision.
inline constexpr uint16_t RequiredCaps = 0x0001;

/// Record offsets (little offsets into each 24-byte record; all multi-byte
/// integers are big-endian).
namespace RecordOffset {
inline constexpr unsigned ProtocolVersion = 0;  // u16
inline constexpr unsigned RecordSizeField = 2;  // u16
inline constexpr unsigned RecordKind = 4;       // u8
inline constexpr unsigned EntryKind = 5;        // u8
inline constexpr unsigned HardwareProfile = 6;  // u8
inline constexpr unsigned SaveProfile = 7;      // u8
inline constexpr unsigned VectorSlot = 8;       // u16
inline constexpr unsigned RequiredCaps = 10;    // u16
inline constexpr unsigned SymbolReference = 12; // 4 bytes zero; RELA type 9
inline constexpr unsigned AssetProfile = 16;    // u32
inline constexpr unsigned Reserved = 20;        // u32, must be zero
} // namespace RecordOffset

//===----------------------------------------------------------------------===//
// A3.3 Record kinds and entry kinds
//===----------------------------------------------------------------------===//

enum RecordKind : uint8_t {
  RK_ISR_ENTRY = 1,    ///< ENTRY, one per compiler ISR definition.
  RK_ISR_REGISTER = 2, ///< REGISTER, paired 1:1 with an ENTRY in one object.
  RK_IRQ_DEFAULT = 3,  ///< Fail-stop default entry, CRT only, asset profile 1.
  RK_IRQ_RESET = 4,    ///< RESET, references the CRT HOME reset symbol.
};

enum EntryKind : uint8_t {
  EK_IRQ_RETI = 1, ///< Normal ISR entry: fixed 37B save, RETI return.
  EK_IRQ_STOP = 2, ///< Fail-stop default entry.
  EK_RESET = 3,    ///< Reset entry.
};

/// Hardware profile 1: 4-byte hardware frame (PSW1, PC[23:16], PC[7:0],
/// PC[15:8]). Software save profile 1: fixed 37-byte integer save.
inline constexpr uint8_t HardwareProfileIRQ4 = 1;
inline constexpr uint8_t SaveProfileINT37 = 1;

/// asset_profile: 0 = compiler-generated ISR, 1 = frozen CRT asset.
inline constexpr uint8_t AssetProfileCompiled = 0;
inline constexpr uint8_t AssetProfileCRT = 1;

/// vector_slot value for records that do not register a slot.
inline constexpr uint16_t NoSlot = 0xFFFF;

//===----------------------------------------------------------------------===//
// A4 Complete slot topology (52 slots).
//===----------------------------------------------------------------------===//

enum class ISRSlotKind : uint8_t {
  Legal = 0,
  Reserved = 1,
  System = 2
};

struct ISRSlotDesc {
  ISRSlotKind Kind;
  uint16_t PDFPage;
};

static constexpr ISRSlotDesc ISRSlots[52] = {
    {ISRSlotKind::Legal,    674}, //  0 INT0
    {ISRSlotKind::Legal,    674}, //  1 Timer0
    {ISRSlotKind::Legal,    674}, //  2 INT1
    {ISRSlotKind::Legal,    674}, //  3 Timer1
    {ISRSlotKind::Legal,    674}, //  4 UART1
    {ISRSlotKind::Legal,    674}, //  5 ADC
    {ISRSlotKind::Legal,    674}, //  6 LVD
    {ISRSlotKind::Reserved, 674}, //  7 gap between 6 and 8
    {ISRSlotKind::Legal,    674}, //  8 UART2
    {ISRSlotKind::Legal,    674}, //  9 SPI
    {ISRSlotKind::Legal,    674}, // 10 INT2
    {ISRSlotKind::Legal,    674}, // 11 INT3
    {ISRSlotKind::Legal,    674}, // 12 Timer2
    {ISRSlotKind::Reserved, 147}, // 13 reserved transfer slot; also absent on 674
    {ISRSlotKind::System,   147}, // 14 system internal
    {ISRSlotKind::System,   147}, // 15 system internal
    {ISRSlotKind::Legal,    674}, // 16 INT4
    {ISRSlotKind::Legal,    674}, // 17 UART3
    {ISRSlotKind::Legal,    674}, // 18 UART4
    {ISRSlotKind::Legal,    674}, // 19 Timer3
    {ISRSlotKind::Legal,    674}, // 20 Timer4
    {ISRSlotKind::Legal,    674}, // 21 CMP
    {ISRSlotKind::Reserved, 674}, // 22 gap between 21 and 24
    {ISRSlotKind::Reserved, 674}, // 23 gap between 21 and 24
    {ISRSlotKind::Legal,    674}, // 24 I2C
    {ISRSlotKind::Legal,    674}, // 25 USB
    {ISRSlotKind::Legal,    674}, // 26 PWMA
    {ISRSlotKind::Legal,    674}, // 27 PWMB
    {ISRSlotKind::Legal,    675}, // 28 CANBUS
    {ISRSlotKind::Legal,    675}, // 29 CAN2BUS
    {ISRSlotKind::Legal,    675}, // 30 LINBUS
    {ISRSlotKind::Reserved, 675}, // 31 gap between 30 and 36
    {ISRSlotKind::Reserved, 675}, // 32 gap between 30 and 36
    {ISRSlotKind::Reserved, 675}, // 33 gap between 30 and 36
    {ISRSlotKind::Reserved, 675}, // 34 gap between 30 and 36
    {ISRSlotKind::Reserved, 675}, // 35 gap between 30 and 36
    {ISRSlotKind::Legal,    675}, // 36 RTC
    {ISRSlotKind::Legal,    676}, // 37 P0
    {ISRSlotKind::Legal,    676}, // 38 P1
    {ISRSlotKind::Legal,    676}, // 39 P2
    {ISRSlotKind::Legal,    676}, // 40 P3
    {ISRSlotKind::Legal,    676}, // 41 P4
    {ISRSlotKind::Legal,    676}, // 42 P5
    {ISRSlotKind::Legal,    676}, // 43 P6
    {ISRSlotKind::Legal,    676}, // 44 P7
    {ISRSlotKind::Reserved, 676}, // 45 gap between 44 and 47
    {ISRSlotKind::Reserved, 676}, // 46 gap between 44 and 47
    {ISRSlotKind::Legal,    676}, // 47 DMA_M2M
    {ISRSlotKind::Legal,    676}, // 48 DMA_ADC
    {ISRSlotKind::Legal,    676}, // 49 DMA_SPI
    {ISRSlotKind::Legal,    676}, // 50 DMA_UART1_TX
    {ISRSlotKind::Legal,    676}  // 51 DMA_UART1_RX
};

static_assert(sizeof(ISRSlots) / sizeof(ISRSlots[0]) == 52);

// Frozen statistics: Legal=39, Reserved=11, System=2.
constexpr unsigned countSlots(ISRSlotKind K, unsigned I = 0) {
  return I == 52 ? 0
                 : (ISRSlots[I].Kind == K ? 1u : 0u) + countSlots(K, I + 1);
}
static_assert(countSlots(ISRSlotKind::Legal) == 39);
static_assert(countSlots(ISRSlotKind::Reserved) == 11);
static_assert(countSlots(ISRSlotKind::System) == 2);

/// \return true when \p N is a user-assignable interrupt slot in the frozen
/// 0-51 profile. Slot 13 is not a generic dispatch entry; reserved and system
/// slots are never legal targets for a user ISR registration.
inline constexpr bool isLegalISRSlot(unsigned N) {
  return N < 52 && ISRSlots[N].Kind == ISRSlotKind::Legal;
}

//===----------------------------------------------------------------------===//
// A5 Vector area and default (fail-stop) entry.
//===----------------------------------------------------------------------===//

inline constexpr uint32_t ISRVectorBase = 0xFF0003;
inline constexpr uint32_t ISRVectorStride = 8;
inline constexpr uint32_t ISRVectorCount = 52;
inline constexpr uint32_t ISRVectorEnd = ISRVectorBase + ISRVectorStride * ISRVectorCount;
static_assert(ISRVectorEnd == 0xFF01A3);

/// Frozen machine bytes of the default IRQ entry:
///   clr EA
///   halt: sjmp halt
inline constexpr uint8_t IRQDefaultBytes[4] = {0xC2, 0xAF, 0x80, 0xFE};

} // end namespace MCS251ISR
} // end namespace llvm

#endif // LLVM_BINARYFORMAT_MCS251ISR_H
