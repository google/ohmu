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

#include "instr.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdio.h>
#include <string>
#include <vector>


//enum ConditionCode {
//	O, NO, B, NAE = B, C = B, NB, AE = NB, NC = NB, Z, E = Z, NZ, NE = NZ, BE, NA = BE, NBE, A = NBE,
//	S, NS, P, PE = P, NP, PO = NP, L, NGE = L, NL, GE = NL, LE, NG = LE, NLE, G = NLE
//};

namespace {
typedef unsigned char byte;
using std::string;
using std::vector;

static const char* const REGCLASS_REGNAME[] = { "", "", "MMXReg", "GP8Reg", "GP16Reg", "GP32Reg", "GP64Reg", "XMMReg", "YMMReg" };
static const char* const REGCLASS_MEMNAME[] = { "", "", "Mem64", "Mem8", "Mem16", "Mem32", "Mem64", "Mem128", "Mem256" };
static const char* const REGCLASS_IMM_TYPE[] = { "", "", "", "char", "short", "int", "long long" };
static const char* const REGCLASS_D64NAME[] = { "", "", "Disp64_64", "Disp64_8", "Disp64_16", "Disp64_32", "Disp64_64", "Disp64_128", "Disp64_256" };

enum { NO_ARGS = 2, MMX = 4, SSE = 128, AVX = 256 };
enum { ALLOW_LOCK = 1, USE_REX = 2, ALLOW_IMM64 = 4, USE_DISP64 = 8, USE_RIP = 16, FIXED_BASE = 32 };

struct InstrBuilder : Instr {
  enum { BASE_RSP = 4, BASE_RBP = 5, INDEX_NONE = 4, BASE_0 = 16 };
  InstrBuilder() : Instr(0, 0, 0) {}
  InstrBuilder& SetRex() { use_rex = rex_1 = 1; return *this; }
  InstrBuilder& SetLongVex() { long_vex = 7; return *this; }
  InstrBuilder& SetW() { w = 1; SetRex(); return *this; }
  InstrBuilder& SetR() { r = 1; SetRex(); return *this; }
  InstrBuilder& SetX() { x = 1; SetRex(); SetLongVex(); return *this; }
  InstrBuilder& SetB() { b = 1; SetRex(); SetLongVex(); return *this; }
  InstrBuilder& SetOpcode(byte o) { opcode = o; return *this; }
  InstrBuilder& SetO(int a) { opcode = (byte)(a & 7); if (a & 0x08) SetB(); if (a & 0x10) SetW(); return *this; }
  InstrBuilder& SetReg(int a) { reg = a; if (a & 0x08) SetR(); if (a & 0x10) SetW(); return *this; }
  InstrBuilder& SetR(int a) { mod = 3; rm = a; if (a & 0x08) SetB(); if (a & 0x10) SetW(); return *this; }
  InstrBuilder& SetM(int a) { rm = a; if (a & 0x08) SetB(); if (a & 0x10) SetW(); return *this; }
  InstrBuilder& SetVVVV(int a) { vvvv = a; return *this; }
  InstrBuilder& SetSegment(SegmentEncoding a) { segment = a; return *this; }
  InstrBuilder& SetAddressSizeOverride(AddressEncoding a) { addr_prefix = a; return *this; }
  InstrBuilder& SetScale(int a) { scale = a; return *this; }
  InstrBuilder& SetRIP() { rip_addr = fixed_base = 1; return SetM(BASE_RBP).SetFixedBase(); }
  InstrBuilder& SetFixedBase() { fixed_base = 1; return *this; }
  InstrBuilder& SetImmSize(int size) { has_imm = 1; imm_size = size; return *this; }
  InstrBuilder& SetOpSqeuence(int a) {
    if ((a & 0xff) == 0x0f) {
      a >>= 8;
      if ((a & 0xff) == 0x38) {
        code_map = 2;
        a >>= 8;
      }
      else if ((a & 0xff) == 0x3a) {
        code_map = 3;
        a >>= 8;
      }
      else
        code_map = 1;
    }
    SetOpcode((byte)a);
    a >>= 8;
    if (a) {
      has_modrm = 1;
      reg = a;
    }
    return *this;
  }
  InstrBuilder& SetBI(int b, int i) {
    if (b & 8) SetB();
    if (i & 8) SetX();
    has_modrm = 1;
    if (b == BASE_0) {
      rm = BASE_RSP;
      base = BASE_RBP;
      index = i;
      fixed_base = has_sib = 1;
      return *this;
    }
    if (b == BASE_RBP)
      force_disp = 1;
    if (i == INDEX_NONE && b != BASE_RSP)
      rm = b;
    else {
      rm = BASE_RSP;
      base = b;
      index = i;
      has_sib = 1;
    }
    return *this;
  }
};

int Log2(int a) {
  assert(a);
  int i = 0;
  for (; a != 1; i++, a /= 2);
  return i;
}

struct Call {
  enum { USE_R = 1, USE_M = 2, USE_L = 4 };
  Call& PushArg(int Call::*x, int size) {
    this->*x = size;
    args.push_back(x);
    if (size == 0)
      implicit.push_back(x);
    return *this;
  }

  Call& M(int size = 0) { rml_mask = (rml_mask & ~USE_R) | USE_M; return PushArg(&Call::rm_size, size); }
  Call& R(int size = 0) { assert(!(rml_mask & USE_L)); return PushArg(&Call::rm_size, size); }
  Call& RM(int size = 0) { rml_mask |= USE_M; return PushArg(&Call::rm_size, size); }
  Call& Reg(int size = 0) { return PushArg(&Call::reg_size, size); }
  Call& VVVV(int size = 0) { return PushArg(&Call::vvvv_size, size); }
  Call& I(int size = 0) { return PushArg(&Call::imm_size, size); }
  Call& O(const char* name = "rm") { o_name = name; return PushArg(&Call::o_size, 0); }
  Call& AX(int size = 0) { return PushArg(&Call::ax_size, size); }
  Call& CX(int size = 0) { return PushArg(&Call::cx_size, size); }
  Call& D64(int size = 0) { return PushArg(&Call::d64_size, size); }

  Call& Except(const char* condition, const Call& sub) {
    exceptions.push_back(std::make_pair(condition, sub));
    return *this;
  }

  string BuildOpcode(int rml, int size) {
    InstrBuilder b;
    b.SetOpSqeuence(opcode_seq);
    if (flags & USE_RIP) b.rip_addr = 1;
    if (flags & FIXED_BASE) b.fixed_base = 1;
    if (size != 8 && size_mask & 8) b.opcode |= o_size ? 8 : 1;
    if (size == 16) b.size_prefix = 1;
    if (size == 64 && flags & USE_REX) b.SetW();
    if (rm_size || reg_size) b.has_modrm = 1;
    if (vvvv_size) b.use_vex = 1;
    if (imm_size) b.SetImmSize(Log2(imm_size) - 3);
    if (d64_size) b.SetImmSize(3);
    if (rm_size && rml & USE_L) b.lock_rep = LOCK_PREFIX;
    char buffer[22];
    snprintf(buffer, 22, "0x%016llxull", b.instr);
    string out = buffer;
    if (rm_size && rml & USE_M) out += " | rm.instr";
    if (rm_size && rml & USE_R) out += " | SET_R[rm]";
    if (reg_size) out += " | SET_REG[reg]";
    if (o_size) out += " | SET_OPCODEREG[rm]";
    if (vvvv_size) out += " | SET_VVVV[vvvv]";
    return out;
  }

  string BuildAsserts(int rml, int size) {
    string out;
    // Add the assert to make sure that we obey register rules.
    bool rm_8 = (rml & USE_R) && rm_size && rm_size == 8;
    bool reg_8 = reg_size && reg_size == 8;
    if (rm_8 && reg_8) out += " assert((rm < AH || rm > BH || reg < R8) && (reg < AH || reg > BH || rm < R8));";
    if (imm_size == 64 && !(flags & ALLOW_IMM64)) out += " assert((int)imm == imm);";
    if (size == 64 && flags & USE_REX && rm_8) out += " assert(rm < AH || rm > BH);";
    if (size == 64 && flags & USE_REX && reg_8) out += " assert(reg < AH || reg > BH);";
    if (ax_size) out += " assert(ax == 0); (void)ax;";
    if (cx_size) out += " assert(cx == 1); (void)cx;";
    return out;
  }

  string BuildArgs(int rml, int /*size*/) {
    string out;
    if (args.empty())
      return out;
    auto i = args.begin(), e = args.end();
    goto ENTRY;
    do {
      out += ", ";
    ENTRY:
      if (&(this->**i) == &rm_size && rml & USE_M) out += string(REGCLASS_MEMNAME[Log2(rm_size)]) + " rm";
      if (&(this->**i) == &rm_size && rml & USE_R) out += string(REGCLASS_REGNAME[Log2(rm_size)]) + " rm";
      if (&(this->**i) == &imm_size) out += string(REGCLASS_IMM_TYPE[Log2(imm_size)]) + " imm";
      if (&(this->**i) == &reg_size) out += string(REGCLASS_REGNAME[Log2(reg_size)]) + " reg";
      if (&(this->**i) == &o_size) out += string(REGCLASS_REGNAME[Log2(o_size)]) + " " + o_name;
      if (&(this->**i) == &vvvv_size) out += string(REGCLASS_REGNAME[Log2(vvvv_size)]) + " vvvv";
      if (&(this->**i) == &ax_size) out += string(REGCLASS_REGNAME[Log2(ax_size)]) + " ax";
      if (&(this->**i) == &cx_size) out += string(REGCLASS_REGNAME[Log2(cx_size)]) + " cx";
      if (&(this->**i) == &d64_size) out += string(REGCLASS_D64NAME[Log2(d64_size)]) + " d64";
    } while (++i != e);
    return out;
  }

  string BuildExceptions(int rml, int size) {
    string out;
    for (auto i = exceptions.begin(), e = exceptions.end(); i != e; ++i)
      if (i->second.IsValidMode(rml, size)) {
        i->second.SetSize(size);
        out += string(i->first) + " ? " + i->second.BuildOpcode(rml, size) + " : ";
      }
    return out;
  }

  string BuildImm(int /*rml*/, int /*size*/) {
    return
      flags & USE_DISP64 ? "(int)d64.disp" :
      imm_size ? "(int)imm" : "0";
  }

  string BuildDisp(int rml, int /*size*/) {
    return
      imm_size == 64 && (flags & ALLOW_IMM64) ? "(int)(imm >> 32)" :
      flags & USE_DISP64 ? "(int)(d64.disp >> 32)" :
      rml & USE_M ? "(int)rm.disp" : "0";
  }

  string BuildCall(int rml, int size) {
    assert(!(flags & ALLOW_IMM64 && rml & USE_M));
    assert(size == NO_ARGS || !(size_mask & NO_ARGS));
    assert(size != NO_ARGS || args.empty());
    string name_s = string(rml & USE_L ? "LOCK_" : "") + name;
    string args_s = BuildArgs(rml, size);
    string assert_s = BuildAsserts(rml, size);
    string exception_s = BuildExceptions(rml, size);
    string opcode_s = BuildOpcode(rml, size);
    string imm_s = BuildImm(rml, size);
    string disp_s = BuildDisp(rml, size);
    return string("\tX64Builder& ") + name_s + "(" + args_s + ") {" + assert_s + " PushBack(Instr(" + exception_s + opcode_s + ", " + imm_s + ", " + disp_s + ")); return *this; }\n";
  }

  bool IsValidMode(int rml, int size) {
    return rml & rml_mask && size & size_mask;
  }

  void SetSize(int size) {
    for (auto i = implicit.begin(), e = implicit.end(); i != e; ++i)
      this->**i = size;
  }

  Call(const string& name, int opcode_seq, int size_mask = NO_ARGS, int flags = 0)
    : name(name)
    , opcode_seq(opcode_seq)
    , size_mask(size_mask)
    , rml_mask(USE_R)
    , flags(flags)
    , rm_size(0)
    , reg_size(0)
    , o_size(0)
    , imm_size(0)
    , vvvv_size(0)
    , ax_size(0)
    , cx_size(0)
    , d64_size(0) {
    if (flags & ALLOW_LOCK) rml_mask |= USE_M | USE_L;
    if ((size_mask & 96) == 96) this->flags |= USE_REX;
  }

  ~Call() {
    if (name == "")
      return;
    for (int size = NO_ARGS; size <= AVX; size *= 2) {
      if (!(size_mask & size)) continue;
      SetSize(size);
      if (rml_mask & USE_R) list.push_back(BuildCall(rml_mask & USE_R, size));
      if (rml_mask & USE_M) list.push_back(BuildCall(rml_mask & USE_M, size));
      if (rml_mask & USE_L) list.push_back(BuildCall(rml_mask & (USE_M | USE_L), size));
    }
  }

  string name;
  const char* o_name;
  string imp_reg_name;
  vector<std::pair<const char*, Call>> exceptions;
  vector<int Call::*> args;
  vector<int Call::*> implicit;
  int opcode_seq;
  int size_mask;
  int rml_mask;
  int flags;
  int rm_size;
  int reg_size;
  int o_size;
  int imm_size;
  int vvvv_size;
  int ax_size;
  int cx_size;
  int d64_size;
  static vector<string> list;
};

vector<string> Call::list;

struct RegCode { int code; };
int operator |(RegCode a, int b) { assert((unsigned)b <= 0xff); return a.code << 8 | b; };
} // namespace {

int main() {
  //char buffer[4096];
  //size_t num_bytes;
  FILE* template_cpp = fopen("template.cpp", "rb");
  FILE* instr_h = fopen("instr.h", "rb");
  FILE* f = fopen("x64builder.h", "wb");
  if (!instr_h || !template_cpp || !f) {
    if (instr_h) fclose(instr_h); else fprintf(stderr, "Failed to open instr.h\n");
    if (template_cpp) fclose(template_cpp); else fprintf(stderr, "Failed to open template.cpp\n");
    if (f) fclose(f); else fprintf(stderr, "Failed to open x64builder.h\n");
    return 1;
  }

  // Read template.cpp
  fseek(template_cpp, 0, SEEK_END);
  auto template_size = ftell(template_cpp);
  fseek(template_cpp, 0, SEEK_SET);
  auto template_data = new char[template_size];
  if (fread(template_data, 1, template_size, template_cpp) != template_size) { fprintf(stderr, "Failed to read all of template.cpp in one call to fread.\n"); return 1; }
  if (fclose(template_cpp)) { fprintf(stderr, "Failed to properly close template.cpp.\n"); return 1; }

  // Read instr.h
  fseek(instr_h, 0, SEEK_END);
  auto instr_size = ftell(instr_h);
  fseek(instr_h, 0, SEEK_SET);
  auto instr_data = new char[instr_size];
  if (fread(instr_data, 1, instr_size, instr_h) != instr_size) { fprintf(stderr, "Failed to read all of instr.h in one call to fread.\n"); return 1; }
  if (fclose(instr_h)) { fprintf(stderr, "Failed to properly close instr.h.\n"); return 1; }

  static const char INSTR_KEY[] = "#include \"instr.h\"";
  static const char TABLE_KEY[] = "//#include \"tables.h\"";
  static const char OPS_KEY[] = "//#include \"ops.h\"";
  auto insert_instr_h = strstr(template_data, INSTR_KEY);
  auto insert_table_h = strstr(template_data, TABLE_KEY);
  auto insert_ops_h = strstr(template_data, OPS_KEY);
  if (!insert_instr_h) { fprintf(stderr, "Failed to find insertion point '#include \"instr.h\"' in template.cpp.\n"); return 1; }
  if (!insert_table_h) { fprintf(stderr, "Failed to find insertion point '//#include \"tables.h\"' in template.cpp.\n"); return 1; }
  if (!insert_ops_h) { fprintf(stderr, "Failed to find insertion point '//#include \"ops.h\"' in template.cpp.\n"); return 1; }

  size_t num_bytes = insert_instr_h - template_data;
  if (fwrite(template_data, 1, num_bytes, f) != num_bytes) { fprintf(stderr, "fwrite failed to write all bytes in one go. [0]\n"); return 1; }
  if (fwrite(instr_data, 1, instr_size, f) != instr_size) { fprintf(stderr, "fwrite failed to write all bytes in one go. [1]\n"); return 1; }
  num_bytes = insert_table_h - (insert_instr_h + sizeof(INSTR_KEY));
  if (fwrite(insert_instr_h + sizeof(INSTR_KEY), 1, num_bytes, f) != num_bytes) { fprintf(stderr, "fwrite failed to write all bytes in one go. [2]\n"); return 1; }

  fprintf(f, "\nstatic const unsigned long long SET_SEGMENT[4] = {\n");
  fprintf(f, "  0x%016llxull,", InstrBuilder().SetSegment(DEFAULT_SEGMENT).instr);
  fprintf(f, " 0x%016llxull,", InstrBuilder().SetSegment(DEFAULT_SEGMENT).instr);
  fprintf(f, " 0x%016llxull,", InstrBuilder().SetSegment(FS).instr);
  fprintf(f, " 0x%016llxull,", InstrBuilder().SetSegment(GS).instr);
  fprintf(f, "\n};\n\nstatic const unsigned long long SET_ADDRESSOVERRIDE[2] = {\n");
  fprintf(f, "  0x%016llxull,", InstrBuilder().SetAddressSizeOverride(DEFAULT_ADDRESS_SIZE).instr);
  fprintf(f, " 0x%016llxull,", InstrBuilder().SetAddressSizeOverride(ADDRESS_SIZE_OVERRIDE).instr);
  fprintf(f, "\n};\n\nstatic const unsigned long long SET_REG[24] = {");
  for (int r = 0; r < 24; r++) {
    if (!(r%4)) fprintf(f, "\n ");
    fprintf(f, " 0x%016llxull,", InstrBuilder().SetReg(r).instr);
  }
  fprintf(f, "\n};\n\nstatic const unsigned long long SET_R[24] = {");
  for (int r = 0; r < 24; r++) {
    if (!(r%4)) fprintf(f, "\n ");
    fprintf(f, " 0x%016llxull,", InstrBuilder().SetR(r).instr);
  }
  fprintf(f, "\n};\n\nstatic const unsigned long long SET_OPCODEREG[24] = {");
  for (int r = 0; r < 24; r++) {
    if (!(r%4)) fprintf(f, "\n ");
    fprintf(f, " 0x%016llxull,", InstrBuilder().SetO(r).instr);
  }
  fprintf(f, "\n};\n\nstatic const unsigned long long SET_VVVV[16] = {");
  for (int r = 0; r < 16; r++) {
    if (!(r%4)) fprintf(f, "\n ");
    fprintf(f, " 0x%016llxull,", InstrBuilder().SetVVVV(r).instr);
  }
  fprintf(f, "\n};\n\nstatic const unsigned long long SET_SCALE[4] = {");
  for (int s = 0; s < 4; s++) {
    if (!(s%4)) fprintf(f, "\n ");
    fprintf(f, " 0x%016llxull,", InstrBuilder().SetScale(s).instr);
  }
  fprintf(f, "\n};\n\nstatic const unsigned long long SET_RIP =\n  0x%016llxull;\n", InstrBuilder().SetRIP().instr);
  fprintf(f, "\nstatic const unsigned long long SET_BASEINDEX[16][17] = {");
  for (int i = 0; i < 16; i++)
    for (int b = 0; b < 17; b++) {
    if (!(b%4)) fprintf(f, "\n ");
      fprintf(f, " 0x%016llxull,", InstrBuilder().SetBI(b, i).instr);
    }
  fprintf(f, "\n};\n");

  num_bytes = insert_ops_h - (insert_table_h + sizeof(TABLE_KEY));
  if (fwrite(insert_table_h + sizeof(TABLE_KEY), 1, num_bytes, f) != num_bytes) { fprintf(stderr, "fwrite failed to write all bytes in one go. [3]\n"); return 1; }

  static const struct { const char* name; int code; } CC_TABLE[] = {
    { "O", 0 }, { "NO", 1 }, { "B", 2 }, { "NAE", 2 }, { "C", 2 }, { "NB", 3 }, { "AE", 3 }, { "NC", 3 },
    { "Z", 4 }, { "E", 4 }, { "NZ", 5 }, { "NE", 5 }, { "BE", 6 }, { "NA", 6 }, { "NBE", 7 }, { "A", 7 },
    { "S", 8 }, { "NS", 9 }, { "P", 10 }, { "PE", 10 }, { "NP", 11 }, { "PO", 11 },
    { "L", 12 }, { "NGE", 12 }, { "NL", 13 }, { "GE", 13 }, { "LE", 14 }, { "NG", 14 }, { "NLE", 15 }, { "G", 15 },
  };
  static const struct { const char* name; int code; int flags; } ALU_TABLE[] = {
    { "ADD", 0x00, ALLOW_LOCK }, { "OR", 0x08, ALLOW_LOCK }, { "ADC", 0x10, ALLOW_LOCK }, { "SBB", 0x18, ALLOW_LOCK },
    { "AND", 0x20, ALLOW_LOCK }, { "SUB", 0x28, ALLOW_LOCK }, { "XOR", 0x30, ALLOW_LOCK }, { "CMP", 0x38, 0 }
  };
  static const struct { const char* name; RegCode reg; } SHIFT_TABLE[] = {
    { "ROL", {0} }, { "ROR", {1} }, { "RCL", {2} }, { "RCR", {3} }, { "SHL", {4} }, { "SAL", {4} }, { "SHR", {5} }, { "SAR", {7} }
  };

  Call("JMP", 0x4ff, 64).RM();
  Call("JMP", 0x0e9, 32, USE_RIP).I();
  //.Except("(char)imm == imm", Call("", 0xeb, 8).I());
  Call("CALL", 0x2ff, 64).RM();
  Call("CALL", 0xe8, 32, USE_RIP | FIXED_BASE).I();
  Call("RET", 0xc3);
  Call("RET", 0xc2, 16).I();

  for (int i = 0; i < 30; i++) {
    auto p = &CC_TABLE[i];
    Call(string("J") + p->name, (0x80 | p->code) << 8 | 0x0f, 32, USE_RIP).I();
    //.Except("(char)imm == imm", Call("", 0x70 | p->code, 8).I());
    Call(string("CMOV") + p->name, (0x40 | p->code) << 8 | 0x0f, 16 | 32 | 64).Reg().RM();
    Call(string("SET") + p->name, (0x90 | p->code) << 8 | 0x0f, 8).RM();
  }

  Call("NOP", 0x90);
  // Add multibyte nop
  Call("INT", 0xcd, 8).I()
    .Except("imm == 3", Call("", 0xcc));

  Call("PUSH", 0x6a, 8).I();
  Call("PUSH", 0x68, 16 | 32).I();
  Call("PUSH", 0x50, 16 | 64).O();
  Call("PUSH", 0x6ff, 16 | 64).M();
  Call("POP", 0x8f, 16 | 64).M();
  Call("POP", 0x58, 16 | 64).O();

  Call("LEA", 0x8d, 16 | 32 | 64).Reg().M(8);

  for (int i = 0; i < 8; i++) {
    auto p = &ALU_TABLE[i];
    RegCode reg = { i };
    Call(p->name, p->code, 8 | 16 | 32 | 64, p->flags).RM().Reg();
    Call(p->name, p->code | 0x02, 8 | 16 | 32 | 64, p->flags).Reg().M();
    Call(p->name, reg | 0x80, 8 | 16 | 32 | 64, p->flags).RM().I()
      .Except("(char)imm == imm", Call("", reg | 0x83, 16 | 32 | 64, p->flags).RM().I(8))
      .Except("rm == 0", Call("", p->code | 0x04, 8 | 16 | 32 | 64).I());
  }

  Call("MOV", 0x88, 8 | 16 | 32 | 64).RM().Reg();
  Call("MOV", 0x8a, 8 | 16 | 32 | 64).Reg().M();
  Call("MOV", 0xa2, 8 | 16 | 32 | 64, USE_DISP64).D64().AX();
  Call("MOV", 0xa0, 8 | 16 | 32 | 64, USE_DISP64).AX().D64();
  Call("MOV", 0xc6, 8 | 16 | 32 | 64).M().I();
  Call("MOV", 0xb0, 8 | 16 | 32 | 64, ALLOW_IMM64).O().I()
    .Except("(unsigned int)imm == imm", Call("", 0xb8, 64).O().I(32)) // note: this is actually a 32-bit instruction
    .Except("(int)imm == imm", Call("", 0xc7, 64, USE_REX).R().I(32));

  Call("TEST", 0x84, 8 | 16 | 32 | 64).RM().Reg();
  Call("TEST", 0x84, 8 | 16 | 32 | 64).Reg().M();
  Call("TEST", 0xf6, 8 | 16 | 32 | 64).M().I();
  Call("TEST", 0xf6, 8 | 16 | 32 | 64).R().I()
    .Except("rm == 0", Call("", 0xa8, 8 | 16 | 32 | 64).I());

  Call("LOCKXCHG", 0x86, 8 | 16 | 32 | 64).M().Reg();
  Call("LOCKXCHG", 0x86, 8 | 16 | 32 | 64).Reg().M();
  Call("XCHG", 0x86, 8 | 16 | 32 | 64).R().Reg()
    .Except("reg == 0", Call("", 0x90, 16 | 32 | 64).O())
    .Except("rm  == 0", Call("", 0x90, 16 | 32 | 64).O("reg"));

  for (int i = 0; i < 8; i++) {
    auto p = &SHIFT_TABLE[i];
    Call(p->name, p->reg | 0xd2, 8 | 16 | 32 | 64).RM().CX(8);
    Call(p->name, p->reg | 0xc0, 8 | 16 | 32 | 64).RM().I()
      .Except("imm == 1", Call("", p->reg | 0xd0, 8 | 16 | 32 | 64).RM());
  }

  Call("INC", 0x0fe, 8 | 16 | 32 | 64).RM();
  Call("DEC", 0x1fe, 8 | 16 | 32 | 64).RM();
  Call("NOT", 0x2f6, 8 | 16 | 32 | 64).RM();
  Call("NEG", 0x3f6, 8 | 16 | 32 | 64).RM();

  Call("MUL", 0x4f6, 8 | 16 | 32 | 64).AX().RM();
  Call("IMUL", 0x5f6, 8).AX().RM();
  Call("IMUL", 0xaf0f, 16 | 32 | 64).Reg().RM()
    .Except("reg == 0", Call("", 0x5f7, 16 | 32 | 64).RM());
  Call("IMUL", 0x69, 16 | 32 | 64).Reg().RM().I()
    .Except("(char)imm == imm", Call("", 0x6b, 16 | 32 | 64).Reg().RM().I(8));
  Call("DIV", 0x6f6, 8 | 16 | 32 | 64).AX().RM();
  Call("IDIV", 0x7f6, 8 | 16 | 32 | 64).AX().RM();

  Call("MOVZX", 0xb60f, 16 | 32 | 64).Reg().RM(8);
  Call("MOVZX", 0xb70f, 32 | 64).Reg().RM(16);
  Call("MOVSX", 0xbe0f, 16 | 32 | 64).Reg().RM(8);
  Call("MOVSX", 0xbf0f, 32 | 64).Reg().RM(16);
  Call("MOVSXD", 0x63, 64, USE_REX).Reg().RM(32);

  Call("CBW", 0x98, 16);
  Call("CWDE", 0x98, 32);
  Call("CDQE", 0x98, 64, USE_REX);
  Call("CWD", 0x99, 16);
  Call("CDQ", 0x99, 32);
  Call("CQO", 0x99, 64, USE_REX);

  //LOCK cmpxchg8|16|32|64|128, BTC|R|S, XADD

  // MOV to COND/SEG, MOVS, CMPS, XLAT, LOOP(N)E, J(E/R)CXZ, IN, OUT, INS, OUTS, STOS, LODS, far RET, IRET, ST/CL C/I/D, RC(L/R)

  std::sort(Call::list.begin(), Call::list.end());
  for (auto i = Call::list.begin(); i != Call::list.end(); ++i)
    fprintf(f, "%s", i->c_str());

  num_bytes = template_size - (insert_ops_h + sizeof(OPS_KEY) - template_data);
  if (fwrite(insert_ops_h + sizeof(INSTR_KEY), 1, num_bytes, f) != num_bytes) { fprintf(stderr, "fwrite failed to write all bytes in one go. [4]\n"); return 1; }

  if (fclose(f)) { fprintf(stderr, "Failed to close output file.\n"); return 1; }
}
