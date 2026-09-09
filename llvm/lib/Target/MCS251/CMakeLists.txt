add_llvm_component_group(MCS251)

set(LLVM_TARGET_DEFINITIONS MCS251.td)

tablegen(LLVM MCS251GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM MCS251GenCallingConv.inc -gen-callingconv)
tablegen(LLVM MCS251GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM MCS251GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM MCS251GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM MCS251GenSDNodeInfo.inc -gen-sd-node-info)
tablegen(LLVM MCS251GenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(MCS251CommonTableGen)

add_llvm_target(MCS251CodeGen
  MCS251AsmPrinter.cpp
  MCS251BranchRelaxation.cpp
  MCS251FrameLowering.cpp
  MCS251InstrInfo.cpp
  MCS251ISelDAGToDAG.cpp
  MCS251ISelLowering.cpp
  MCS251MCInstLower.cpp
  MCS251RegisterInfo.cpp
  MCS251Subtarget.cpp
  MCS251TargetMachine.cpp
  MCS251TargetObjectFile.cpp

  LINK_COMPONENTS
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  MCS251Desc
  MCS251Info
  SelectionDAG
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  MCS251
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
