set(LLVM_LINK_COMPONENTS
  BinaryFormat
  TargetParser
  )

add_llvm_unittest(BinaryFormatTests
  DwarfTest.cpp
  ELFTest.cpp
  MCS251AttributesTest.cpp
  MCS251SignaturesTest.cpp
  MachOTest.cpp
  MsgPackDocumentTest.cpp
  MsgPackReaderTest.cpp
  MsgPackWriterTest.cpp
  SFrameTest.cpp
  TestFileMagic.cpp
  )

