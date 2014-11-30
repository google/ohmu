//===- ASTNode.h -----------------------------------------------*- C++ --*-===//
// Copyright 2014  Google
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//===----------------------------------------------------------------------===//
// Fixed width x64 instruction.
// This file defines a 128 bit instruction that can encode all of the
// information for any x64 instruction in a fixed format.  The first 8 bytes
// store all of the prefixes, the opcode, the register references and the
// addressing mode.  The second 8 bytes contain displacement and or immediate
// information.
//
// This file also defines an encode function that encodes the fixed width format
// into the variable width format that can be executed.
//
// This format and encoding are designed to be both compact and efficient and
// sacrifice some amount of readability to do so.  We do not expect any of this
// to be readable by someone without understanding of x64 encoding.
//
// OSDev reference:
// http://wiki.osdev.org/X86-64_Instruction_Encoding
//
// Intel reference manual:
// http://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-
// architectures-software-developer-instruction-set-reference-manual-325383.pdf
//===----------------------------------------------------------------------===//
#pragma once

enum SegmentEncoding { DEFAULT_SEGMENT, INVALD_SEGMENT, FS, GS };
enum LockRepEncoding { NO_LOCKREP, LOCK_PREFIX, REPZ_PREFIX, REPNZ_PREFIX };
enum AddressEncoding { DEFAULT_ADDRESS_SIZE, ADDRESS_SIZE_OVERRIDE };

struct /*__declspec(align(16))*/ Instr {
  typedef unsigned char byte;

  Instr() {}
  Instr(unsigned long long instr, int imm32, int disp32)
    : instr(instr), imm32(imm32), disp32(disp32) {}

  byte* encode(byte* p) const;

  union {
    struct {
      // Vex optional byte.
      byte code_map : 2;
      byte evex : 1;
      byte invalid : 1;
      byte r1 : 1;
      byte long_vex : 3;

      // The opcode/raw data info.
      byte align_pad : 4;
      byte raw_data : 1;
      byte : 3;

      // Postfixes flags.
      byte imm_size : 2;
      byte has_imm : 1;
      byte rip_addr : 1;
      byte has_modrm : 1;
      byte has_sib : 1;
      byte fixed_base : 1;
      byte force_disp : 1;

      // Prefixes flags.
      byte lock_rep : 2;
      byte size_prefix : 1;
      byte addr_prefix : 1;
      byte use_vex : 1;
      byte use_rex : 1;
      byte segment : 2;

      // Rex byte.
      byte b : 1;
      byte x : 1;
      byte r : 1;
      byte w : 1;
      byte : 2;
      byte rex_1 : 1;
      byte : 1;

      // Vex byte.
      byte simd_prefix : 2;
      byte l : 1;
      byte vvvv : 4;
      byte e : 1; // vex verion of W

      // Modrm.
      byte rm : 3;
      byte reg : 3;
      byte mod : 2;

      // Sib.
      byte base : 3;
      byte index : 3;
      byte scale : 2;
    };
    struct {
      byte vex1;
      byte opcode;
      byte flags;
      byte prefix;
      byte rex;
      byte vex2;
      byte modrm;
      byte sib;
    };
    struct {
      unsigned long long instr;
      int imm32;
      int disp32;
    };
  };
};

inline Instr::byte* Instr::encode(byte* p) const {
  if (invalid) {
    if (raw_data) goto ENCODE_NO_DISP;
    return p;
  }
  // Handle all prefixes, including rex and vex.
  if (prefix) {
    // Handle legacy prefixes.
    if (prefix & 0xcf) {
      if (segment) *p++ = segment ^ 0x66;
      if (lock_rep) *p++ = lock_rep ^ 0xf1;
      if (size_prefix) *p++ = 0x66;
      if (addr_prefix) *p++ = 0x67;
    }
    // Handle vex prefix.
    if (use_vex) {
      byte rxb = rex << 5;
      if (!long_vex) {
        // Encode 2-byte vex prefix.
        *p++ = 0xc5;
        *p++ = rxb ^ vex2 ^ 0x80;
        goto ENCODE_OPCODE;
      }
      // Encode 3-byte vex prefix.
      *p++ = 0xc4;
      *p++ = rxb ^ vex1;
      *p++ = vex2;
      goto ENCODE_OPCODE;
    }
    // Encode rex prefix.
    if (use_rex) *p++ = rex;
  }
  // Handle opcode-prefixes.
  if (code_map) {
    *p++ = 0x0f;
    if (code_map & 0x02)
      *p++ = (vex1 ^ 0xfe) << 1; // high bits of vex1 are 0
  }
ENCODE_OPCODE:
  //Encode the opcode.
  *p++ = opcode;
  if (!has_modrm) goto ENCODE_NO_DISP;
  {
    // Save the location of the modrm byte.
    byte* pmod = p;
    // Encode the modrm byte.
    *p++ = modrm;
    if (mod) goto ENCODE_NO_DISP;
    // Handle the sib byte.
    if (has_sib) *p++ = sib;
    if (fixed_base) goto ENCODE_DISP32;
    if (disp32 == 0 && !force_disp) goto ENCODE_NO_DISP;
    if ((char)disp32 == disp32) {
      // Encode an 8-bit displacement.
      *pmod |= 0x40;
      *p++ = (byte)disp32;
      goto ENCODE_NO_DISP;
    }
    *pmod |= 0x80;
  }
ENCODE_DISP32:
  // Encode a 32-bit displacement.
  *(int*)p = disp32;
  p += 4;
ENCODE_NO_DISP:
  // Encode an immediate.
  if (has_imm) {
    if (imm_size == 0) { *p++ = (byte)imm32; }
    else if (imm_size == 2) { *(int*)p = imm32; p += 4; }
    else if (imm_size <  2) { *(short*)p = (short)imm32; p += 2; }
    else                    { *(long long*)p = *(long long*)&imm32; p += 8; }
  }
  return p;
}
