//===- llvm/BinaryFormat/MCS251ISR.h - MCS251 ISR protocol ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Shared, frozen constants for the MCS-251 interrupt campaign.  This header is
// the single registration point for:
//   - the `.mcs251.isr` object metadata section layout,
//   - the complete 127-slot interrupt topology with per-slot evidence,
//   - the vector-area formulas, the BOOT floor and the frozen default
//     (fail-stop) entry,
//   - the one canonical decimal slot parser used by the Verifier, the target
//     contract check and the AsmPrinter.
//
// Names, numbers, byte layout, slot classification and semantics are frozen.
// Changes may only be made via the design owner through PM arbitration.
//
// G1 profile (PM ruling 2026-09-13, "暂时按照推荐设计"): the topology is the
// STC32G144K246 evidence profile 0..126 (127 slots, Legal=109 / Reserved=16 /
// System=2).  It is the G144K246 number table; it does NOT claim every legacy
// STC32G board carries the newly added peripherals.  The evidence class of
// each slot records WHY it is classified the way it is (`DualSource`: manual
// section plus official header; `HeaderOnly`: official header only, no manual
// row; `NoSource`: neither; `LegacySpecial`: a legacy-family special slot).
// Only `Kind` is enforcement; the evidence fields exist for audit and never
// enter the 24-byte serialization.
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

/// protocol_version of this revision.  G1 raised it 1 -> 2: the field width
/// and the 24-byte layout are unchanged, but the topology, the default vector
/// coverage and the link layout all changed, so a reader must reject the old
/// records explicitly rather than silently honour only the legacy 52 slots.
/// This is NOT the ELF ABI v2, the ASLayoutVersion or the
/// .mcs251.attributes ObjectProtocolVersion.
inline constexpr uint16_t ProtocolVersion = 2;
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
// A4 Complete slot topology (127 slots, G144K246 evidence profile).
//===----------------------------------------------------------------------===//

enum class ISRSlotKind : uint8_t {
  Legal = 0,
  Reserved = 1,
  System = 2
};

/// Why a slot carries the classification it does.  Audit only.
enum class ISRSlotEvidence : uint8_t {
  DualSource = 0,   ///< Official header macro AND a manual section 15.3 row.
  HeaderOnly = 1,   ///< Official header macro, no manual section 15.3 row.
  NoSource = 2,     ///< Neither source names this number.
  LegacySpecial = 3 ///< Legacy-family special slot (7 / 13 / 14 / 15).
};

struct ISRSlotDesc {
  ISRSlotKind Kind;   ///< The only enforcement field.
  ISRSlotEvidence Evidence;
  /// Line of the first macro in STC32G144K246.H, 0 when there is none.
  uint16_t HeaderLine;
  /// Line of this slot's row in manual section 15.3, 0 when there is none.
  uint16_t ManualLine;
};

// Dense, explicit, 1:1 with the slot number, so a missing tail row cannot be
// silently zero-initialized into "Legal".  Evidence lines are from the
// G144K246 header (H) and manual 15.3 (M); aliases share one slot and the
// header line names the first spelling.
// The length is deliberately derived, not stated: with an explicit [127] a
// deleted row would be zero-initialized back into kind 0 (Legal) and every
// statistics assert would still hold.  An unsized array makes the
// ISRVectorCount == 127 static_assert fail on the very next compile.
static constexpr ISRSlotDesc ISRSlots[] = {
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2755,  181}, //   0 INT0_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2756,  182}, //   1 TMR0_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2757,  183}, //   2 INT1_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2758,  184}, //   3 TMR1_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2759,  185}, //   4 UART1_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2760,  186}, //   5 ADC_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2762,  187}, //   6 LVD_VECTOR
    {ISRSlotKind::Reserved,ISRSlotEvidence::LegacySpecial, 2763,    0}, //   7 PCA_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2764,  188}, //   8 UART2_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2765,  189}, //   9 SPI_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2767,  190}, //  10 INT2_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2768,  191}, //  11 INT3_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2769,  192}, //  12 TMR2_VECTOR
    {ISRSlotKind::Reserved,ISRSlotEvidence::LegacySpecial, 2770,    0}, //  13 USER_VECTOR
    {ISRSlotKind::System, ISRSlotEvidence::LegacySpecial,    0,    0}, //  14 System internal
    {ISRSlotKind::System, ISRSlotEvidence::LegacySpecial,    0,    0}, //  15 System internal
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2771,  193}, //  16 INT4_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2772,  194}, //  17 UART3_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2773,  195}, //  18 UART4_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2774,  196}, //  19 TMR3_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2775,  197}, //  20 TMR4_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2776,  198}, //  21 CMP_VECTOR
    {ISRSlotKind::Reserved,ISRSlotEvidence::NoSource     ,    0,    0}, //  22 (gap)
    {ISRSlotKind::Reserved,ISRSlotEvidence::NoSource     ,    0,    0}, //  23 (gap)
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2778,  199}, //  24 I2C_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2780,  200}, //  25 USB_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2781,  201}, //  26 PWMA_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2782,  202}, //  27 PWMB_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2783,  203}, //  28 CAN1_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2784,  204}, //  29 CAN2_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2785,  205}, //  30 LIN1_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2786,  206}, //  31 LIN2_VECTOR
    {ISRSlotKind::Reserved,ISRSlotEvidence::NoSource     ,    0,    0}, //  32 (gap)
    {ISRSlotKind::Reserved,ISRSlotEvidence::NoSource     ,    0,    0}, //  33 (gap)
    {ISRSlotKind::Reserved,ISRSlotEvidence::NoSource     ,    0,    0}, //  34 (gap)
    {ISRSlotKind::Reserved,ISRSlotEvidence::NoSource     ,    0,    0}, //  35 (gap)
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2788,  207}, //  36 RTC_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2789,  208}, //  37 P0INT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2790,  209}, //  38 P1INT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2791,  210}, //  39 P2INT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2792,  211}, //  40 P3INT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2793,  212}, //  41 P4INT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2794,  213}, //  42 P5INT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2795,  214}, //  43 P6INT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2796,  215}, //  44 P7INT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2797,  216}, //  45 P8INT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2798,  217}, //  46 P9INT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2799,  218}, //  47 DMA_M2M_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2800,  219}, //  48 DMA_ADC_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2802,  220}, //  49 DMA_SPI_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2804,  221}, //  50 DMA_UR1T_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2805,  222}, //  51 DMA_UR1R_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2806,  223}, //  52 DMA_UR2T_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2807,  224}, //  53 DMA_UR2R_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2808,  225}, //  54 DMA_UR3T_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2809,  226}, //  55 DMA_UR3R_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2810,  227}, //  56 DMA_UR4T_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2811,  228}, //  57 DMA_UR4R_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2812,  229}, //  58 DMA_LCM_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2813,  230}, //  59 LCM_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2814,  231}, //  60 DMA_I2CT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2816,  232}, //  61 DMA_I2CR_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2818,  233}, //  62 I2S_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2820,  234}, //  63 DMA_I2ST_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2822,  235}, //  64 DMA_I2SR_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2824,  236}, //  65 DMA_QSPI_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2825,  237}, //  66 QSPI_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2826,  238}, //  67 TMR11_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2827,  239}, //  68 DMA_I2C2T_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2828,  240}, //  69 DMA_I2C2R_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2829,  241}, //  70 DMA_I2S2T_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2830,  242}, //  71 DMA_I2S2R_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2831,  243}, //  72 DMA_PWMAT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2832,  244}, //  73 DMA_PWMAR_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2833,  245}, //  74 DMA_PWMCT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2834,  246}, //  75 DMA_PWMCR_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2835,  247}, //  76 DMA_ADC2_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2836,  248}, //  77 DMA_DAC_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2838,  249}, //  78 DMA_DAC2_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2839,  250}, //  79 DMA_SPI2_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2840,  251}, //  80 DMA_SPI3_VECTOR
    {ISRSlotKind::Reserved,ISRSlotEvidence::HeaderOnly   , 2841,    0}, //  81 DMA_SPI4_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2842,  252}, //  82 DMA_UR5T_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2843,  253}, //  83 DMA_UR5R_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2844,  254}, //  84 DMA_UR6T_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2845,  255}, //  85 DMA_UR6R_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2846,  256}, //  86 DMA_UR7T_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2847,  257}, //  87 DMA_UR7R_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2848,  258}, //  88 DMA_UR8T_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2849,  259}, //  89 DMA_UR8R_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2850,  260}, //  90 PAINT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2851,  261}, //  91 PBINT_VECTOR
    {ISRSlotKind::Reserved,ISRSlotEvidence::HeaderOnly   , 2852,    0}, //  92 PCINT_VECTOR
    {ISRSlotKind::Reserved,ISRSlotEvidence::HeaderOnly   , 2853,    0}, //  93 PDINT_VECTOR
    {ISRSlotKind::Reserved,ISRSlotEvidence::HeaderOnly   , 2854,    0}, //  94 PEINT_VECTOR
    {ISRSlotKind::Reserved,ISRSlotEvidence::HeaderOnly   , 2855,    0}, //  95 PFINT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2856,  264}, //  96 TMR5_TMR6_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2857,  266}, //  97 TMR7_TMR8_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2858,  268}, //  98 TMR9_TMR10_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2859,  270}, //  99 TMR17_TMR18_VECTOR
    {ISRSlotKind::Reserved,ISRSlotEvidence::NoSource     ,    0,    0}, // 100 (no source)
    {ISRSlotKind::Reserved,ISRSlotEvidence::NoSource     ,    0,    0}, // 101 (no source)
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2860,  272}, // 102 UART5_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2861,  273}, // 103 UART6_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2862,  274}, // 104 UART7_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2863,  275}, // 105 UART8_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2864,  276}, // 106 ADC2_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2865,  277}, // 107 DAC_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2867,  278}, // 108 DAC2_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2868,  279}, // 109 I2C2_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2869,  280}, // 110 I2S2_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2870,  281}, // 111 SPI2_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2871,  282}, // 112 SPI3_VECTOR
    {ISRSlotKind::Reserved,ISRSlotEvidence::HeaderOnly   , 2872,    0}, // 113 SPI4_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2873,  283}, // 114 CMP2_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2874,  284}, // 115 CMP3_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2875,  285}, // 116 CMP4_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2876,  286}, // 117 DMA_CANT_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2878,  287}, // 118 DMA_CANR_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2880,  288}, // 119 DMA_CAN2T_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2881,  289}, // 120 DMA_CAN2R_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2882,  290}, // 121 PWMC_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2883,  291}, // 122 PWMD_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2884,  292}, // 123 PWME_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2885,  293}, // 124 PWMF_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2886,  294}, // 125 DMA_PWMET_VECTOR
    {ISRSlotKind::Legal, ISRSlotEvidence::DualSource   , 2887,  297}, // 126 DMA_PWMER_VECTOR
};

//--- Statistics ------------------------------------------------------------
//
// Derived from the table, never hand-written: a stale literal would be a
// second source of truth.  The explicit static_asserts below pin the PM-
// approved numbers, so a table edit that changes them fails the build.

inline constexpr unsigned ISRVectorCount =
    sizeof(ISRSlots) / sizeof(ISRSlots[0]);
static_assert(ISRVectorCount == 127, "G1 profile is 127 slots");

inline constexpr uint32_t ISRVectorMaxSlot = ISRVectorCount - 1;
static_assert(ISRVectorMaxSlot == 126, "G1 profile upper bound is 126");

constexpr unsigned countSlots(ISRSlotKind K, unsigned I = 0) {
  return I == ISRVectorCount
             ? 0
             : (ISRSlots[I].Kind == K ? 1u : 0u) + countSlots(K, I + 1);
}
static_assert(countSlots(ISRSlotKind::Legal) == 109, "Legal count is 109");
static_assert(countSlots(ISRSlotKind::Reserved) == 16, "Reserved is 16");
static_assert(countSlots(ISRSlotKind::System) == 2, "System is 2");
static_assert(countSlots(ISRSlotKind::Legal) +
                      countSlots(ISRSlotKind::Reserved) +
                      countSlots(ISRSlotKind::System) ==
                  ISRVectorCount,
              "every slot must have exactly one kind");

/// \return true when \p N is a user-assignable interrupt slot in the G1
/// profile.  The range test runs first; slot 13 is a reserved transfer slot,
/// never a generic dispatch entry; reserved and system slots are never legal
/// targets.  The parameter is 64-bit so a caller that holds a wider value
/// cannot truncate it into a valid slot number before the check.
inline constexpr bool isLegalISRSlot(uint64_t N) {
  return N < ISRVectorCount && ISRSlots[N].Kind == ISRSlotKind::Legal;
}

/// \return the classification of \p N, or `System` for an out-of-profile
/// number (a caller must test the range separately when it needs to
/// distinguish "reserved inside the profile" from "outside the profile").
inline constexpr ISRSlotKind slotKind(uint64_t N) {
  return N < ISRVectorCount ? ISRSlots[N].Kind : ISRSlotKind::System;
}

//===----------------------------------------------------------------------===//
// A5 Vector area, BOOT floor and default (fail-stop) entry.
//===----------------------------------------------------------------------===//

inline constexpr uint32_t ISRVectorBase = 0xFF0003;
inline constexpr uint32_t ISRVectorStride = 8;
inline constexpr uint32_t ISRVectorEnd =
    ISRVectorBase + ISRVectorStride * ISRVectorCount;
static_assert(ISRVectorEnd == 0xFF03FB, "G1 vector area end is 0xFF03FB");

/// The lowest address the frozen IRQ CRT BOOT asset may occupy.  The whole
/// vector area [ISRVectorBase, ISRVectorEnd) is reserved, so BOOT must start
/// at or above this inclusive floor; the published recipe uses 0xFF0500.
inline constexpr uint32_t ISRBootMinAddress = 0xFF0500;
static_assert(ISRBootMinAddress >= ISRVectorEnd,
              "BOOT floor must not sit inside the reserved vector area");

/// The published IRQ BOOT / CSEG recipe.  These are the values the release
/// assets are generated with; a user link may still supply its own area
/// starts, so lld does NOT treat them as fixed for every input.
inline constexpr uint32_t ISRBootRecipeAddress = 0xFF0500;
inline constexpr uint32_t ISRCsegRecipeAddress = 0xFF0700;

/// Frozen machine bytes of the default IRQ entry:
///   clr EA
///   halt: sjmp halt
inline constexpr uint8_t IRQDefaultBytes[4] = {0xC2, 0xAF, 0x80, 0xFE};

//===----------------------------------------------------------------------===//
// Canonical decimal slot parser (single implementation for all layers).
//
// The Verifier, the target contract check and the AsmPrinter must agree
// exactly; a second copy would drift.  This helper has no dependency beyond
// StringRef so every layer can share it.  It rejects the empty string, a
// sign, surrounding or embedded whitespace, a leading zero (except the single
// character "0"), hexadecimal text and any non-digit; it stops with an
// overflow/slot-bound check BEFORE multiplying, so a wrapping value can never
// be truncated into a legal slot.  A successful parse does NOT imply the slot
// is legal: the caller must still call isLegalISRSlot(), so Reserved/System
// numbers stay rejected.
//===----------------------------------------------------------------------===//

/// \return true when \p Text is the canonical decimal spelling of an
/// in-profile slot number; on success \p Out receives that number.
inline bool parseCanonicalSlot(StringRef Text, uint64_t &Out) {
  if (Text.empty())
    return false;
  // Single "0" is the only spelling with a leading zero; any other leading
  // zero (e.g. "00", "0126") is rejected.
  if (Text[0] == '0' && Text.size() != 1)
    return false;
  uint64_t Val = 0;
  for (char C : Text) {
    if (C < '0' || C > '9')
      return false;
    // Bound before the multiply/add so a long digit string cannot wrap.
    if (Val > (uint64_t(ISRVectorMaxSlot) - uint64_t(C - '0')) / 10)
      return false;
    Val = Val * 10 + uint64_t(C - '0');
  }
  Out = Val;
  return true;
}

} // end namespace MCS251ISR
} // end namespace llvm

#endif // LLVM_BINARYFORMAT_MCS251ISR_H
