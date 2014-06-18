//===- template.cpp --------------------------------------------*- C++ --*-===//
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
// This file is a template for an auto-generated assembly header file.  It
// should compile successfully as it is but contains some commented #includes
// that get replaced by the header generator.
//===----------------------------------------------------------------------===//
#pragma once
#include <cassert>
#include <vector>

#include "instr.h"

// The first 4 must be in this order because they are used as imm_size
enum RegClass { GP8, GP16, GP32, GP64, MMX, XMM, YMM };

enum GP8Reg {
  AL, CL, DL, BL,
  AH, CH, DH, BH,
  R8L, R9L, R10L, R11L,
  R12L, R13L, R14L, R15L,
  SPL = 20, BPL, SIL, DIL,
};

enum GP16Reg {
  AX, CX, DX, BX,
  SP, BP, SI, DI,
  R8W, R9W, R10W, R11W,
  R12W, R13W, R14W, R15W,
};

enum GP32Reg {
  EAX, ECX, EDX, EBX,
  ESP, EBP, ESI, EDI,
  R8D, R9D, R10D, R11D,
  R12D, R13D, R14D, R15D,
};

enum GP64Reg {
  RAX, RCX, RDX, RBX,
  RSP, RBP, RSI, RDI,
  R8, R9, R10, R11,
  R12, R13, R14, R15,
};

enum BaseReg {
  BASE_RAX, BASE_RCX, BASE_RDX, BASE_RBX,
  BASE_RSP, BASE_RBP, BASE_RSI, BASE_RDI,
  BASE_R8, BASE_R9, BASE_R10, BASE_R11,
  BASE_R12, BASE_R13, BASE_R14, BASE_R15,
  BASE_0
};

enum IndexReg {
  INDEX_RAX, INDEX_RCX, INDEX_RDX, INDEX_RBX,
  INDEX_NONE, INDEX_RBP, INDEX_RSI, INDEX_RDI,
  INDEX_R8, INDEX_R9, INDEX_R10, INDEX_R11,
  INDEX_R12, INDEX_R13, INDEX_R14, INDEX_R15,
};

enum RipReg {
  RIP
};

template<int>
struct Mem {
  enum Segment {DEFAULT_SEGMENT, FS, GS};
  enum AddrSize {DEFAULT_SIZE, SIZE_OVERRIDE};
  __forceinline Mem(
    BaseReg base,
    int disp = 0,
    IndexReg index = INDEX_NONE,
    int scale = 0,
    Segment segment = DEFAULT_SEGMENT,
    AddrSize addr_size = DEFAULT_SIZE)
    : instr(SET_BASEINDEX[index][base] | SET_SCALE[scale] | SET_SEGMENT[segment] | SET_ADDRESSOVERRIDE[addr_size])
    , disp(disp) {}
  __forceinline Mem(RipReg, int disp)
    : instr(SET_RIP), disp(disp) {}
  unsigned long long instr;
  int disp;
};

template<int>
struct Disp64 {
  __forceinline Disp64(long long disp)
    : disp(disp) {}
  long long disp;
};

typedef Mem<8> Mem8;
typedef Mem<16> Mem16;
typedef Mem<32> Mem32;
typedef Mem<64> Mem64;
typedef Mem<128> Mem128;
typedef Mem<256> Mem256;
typedef Mem<512> Mem512;

typedef Disp64<8> Disp64_8;
typedef Disp64<16> Disp64_16;
typedef Disp64<32> Disp64_32;
typedef Disp64<64> Disp64_64;
typedef Disp64<128> Disp64_128;
typedef Disp64<256> Disp64_256;
typedef Disp64<512> Disp64_512;

//#include "tables.h"

struct X64Builder {
  typedef unsigned char byte;
  struct Event {
    enum Kind : byte { CANDIDATE, ONEBYTE, LABEL, FOURBYTE };
    Event(unsigned target, char savings, unsigned relaxed, unsigned optimal, unsigned prefix, Kind kind)
      : target(target), savings(savings), relaxed(relaxed), optimal(optimal), prefix(prefix), kind(kind) {}
    byte savings;
    Kind kind;
    unsigned target, relaxed, optimal, prefix;
  };

  X64Builder& Label() {
    if (label_offsets.size() && label_offsets.back() == stream.size())
      fprintf(stderr, "A label already exists at this offset.");
    else
      label_offsets.push_back(stream.size());
    return *this;
  }

  __forceinline X64Builder& PushBack(const Instr& i) {
    stream.push_back(i);
    return *this;
  }

  X64Builder& Reset() {
    stream.clear();
    label_offsets.clear();
    return *this;
  }

  byte* EncodeNoRIP(byte* out) {
    if (stream.size() == 0)
      return out;
    if (label_offsets.size() && label_offsets.back() == stream.size())
      return ErrorTrailingLabel(out);
    for (auto i = stream.begin(), e = stream.end(); i != e; ++i)
      out = i->encode(out);
    return out;
  }

  byte* Encode(byte* out) const {
    if (stream.size() == 0)
      return out;
    if (label_offsets.size() && label_offsets.back() == stream.size())
      return ErrorTrailingLabel(out);
    auto hold = out;
    std::vector<unsigned> patch(label_offsets.size(), 0);
    size_t label = 0;
    for (size_t i = 0, e = stream.size(), label_max = label_offsets.size(); i != e; i++) {
      if (label < label_max && label_offsets[label] == i) {
        for (auto offset = patch[label]; offset;) {
          auto temp = ((unsigned*)(hold + offset))[-1];
          ptrdiff_t delta = out - (hold + offset);
          if ((int)delta != delta)
            return ErrorCantEncodeDelta(hold, delta);
          ((int*)(hold + offset))[-1] = (int)delta;
          offset = temp;
        }
        if ((unsigned)(out - hold) != out - hold)
          return ErrorBinaryTooLarge(hold);
        patch[label++] = (unsigned)(out - hold);
      }
      out = stream[i].encode(out);
      if (!stream[i].rip_addr)
        continue;
      auto& index = ((unsigned*)out)[-1];
      if (index >= label_offsets.size())
        return ErrorIndexOutOfRange(hold, index);
      if (index >= label) {
        auto temp = patch[index];
        if ((unsigned)(out - hold) != out - hold)
          return ErrorBinaryTooLarge(hold);
        patch[index] = (unsigned)(out - hold);
        index = temp;
        continue;
      }
      ptrdiff_t delta = hold + patch[index] - out;
      if ((int)delta != delta)
        return ErrorCantEncodeDelta(hold, delta);
      if (stream[i].fixed_base) {
        ((int*)out)[-1]= (int)delta;
        continue;
      }
      byte size = (out[-5] != 0xe9 ? 1 : 0) + 3;
      if ((char)(delta + size) != delta + size) {
        index = (int)delta;
        continue;
      }
      delta += size;
      out -= size;
      out[-2] = size == 3 ? 0xeb : out[-1] - 0x10;
      out[-1] = (char)delta;
    }
    return out;
  }

  byte* EncodeRelaxed(byte* const out) const {
    if (stream.size() == 0)
      return out;
    if (label_offsets.size() && label_offsets.back() == stream.size())
      return ErrorTrailingLabel(out);
    unsigned relaxed, optimal, prior;
    byte* hold,* dst,* src;
    std::vector<Event> events;
    std::vector<unsigned> targets(label_offsets.size(), 0);
    events.reserve(stream.size() + label_offsets.size());
    if ((unsigned)events.size() != events.size()) {
      fprintf(stderr, "Event list too big.\n");
      return out;
    }
    // Initialize the events.
    relaxed = optimal = 0;
    hold = dst = out;
    for (size_t i = 0, e = stream.size(), l = 0, l_max = label_offsets.size(); i != e; i++) {
      // If our next label is here, insert it.
      if (l < l_max && label_offsets[l] == i) {
        for (auto x = targets[l]; x; ) {
          auto& event = events[x];
          x = event.target;
          event.target = (unsigned)events.size();
        }
        targets[l] = (unsigned)events.size();
        ptrdiff_t delta = dst - hold;
        auto prefix = (unsigned)delta;
        if (prefix != delta)
          return ErrorCantEncodeDelta(out, delta);
        relaxed += prefix;
        optimal += prefix;
        hold = dst;
        events.push_back(Event(targets[l], 0, relaxed, optimal, prefix, Event::LABEL));
        l++;
      }
      // Encode the instruction
      dst = stream[i].encode(dst);
      // If the instruction doesn't involve IP relative addresses continue.
      if (!stream[i].rip_addr)
        continue;
      // Get the target
      auto target_index = ((unsigned*)dst)[-1];
      if ((size_t)target_index >= label_offsets.size())
        return ErrorIndexOutOfRange(out, target_index);
      // calculate the prefix and update relaxed and optimal
      ptrdiff_t delta = dst - hold;
      auto prefix = (unsigned)delta;
      if (prefix != delta)
        return ErrorCantEncodeDelta(out, delta);
      relaxed += prefix;
      optimal += prefix;
      hold = dst;
      auto next = targets[target_index];
      if (target_index >= l)
        targets[target_index] = (unsigned)events.size();
      byte savings = 0;
      Event::Kind kind = Event::CANDIDATE;
      if (stream[i].fixed_base)
        kind = Event::FOURBYTE;
      else {
        // Calculate potential savings from relaxation
        savings = dst[-5] == 0xe9 ? 3 : 4;
        auto& target = events[next];
        // If we're not a relative jump we can't be relaxed to
        // If we're jumping too far back to ever relax this jump, don't bother to push it as an event.
        if (target_index < l && optimal - target.optimal > 128u + savings) {
          kind = Event::FOURBYTE;
          savings = 0;
        } else {
          optimal -= savings;
          if (target_index < l && relaxed - target.relaxed <= 128u + savings) {
            relaxed -= savings;
            kind = Event::ONEBYTE;
          }
        }
      }
      events.push_back(Event(next, savings, relaxed, optimal, prefix, kind));
    }
    ptrdiff_t delta = dst - hold;
    auto postfix = (unsigned)delta;
    if (postfix != delta)
      return ErrorCantEncodeDelta(out, delta);
    // Perform relaxation.
    do {
      // Set the initial conditional for a pass.
      prior = relaxed - optimal;
      relaxed = optimal = 0;
      // Execute a relaxation pass.
      static const int EVENT_ID = 0;
      for (size_t i = 0, e = events.size(); i != e; ++i) {
        auto& event = events[i];
        optimal += event.prefix;
        relaxed += event.prefix;
        if (event.kind != Event::CANDIDATE) {
          optimal -= event.savings;
          relaxed -= event.savings;
        } else {
          const auto& target = events[event.target];
          if (event.target > i) {
            // Forward jump event.
            if (target.optimal - event.optimal > 127) {
              event.savings = 0;
              event.kind = Event::FOURBYTE;
            } else {
              optimal -= event.savings;
              if (target.relaxed - event.relaxed <= 127) {
                relaxed -= event.savings;
                event.kind = Event::ONEBYTE;
              }
            }
          } else {
            // Back jump event.
            if (optimal - target.optimal > 128u + target.savings) {
              event.savings = 0;
              event.kind = Event::FOURBYTE;
            } else {
              optimal -= event.savings;
              if (relaxed - target.relaxed <= 128u + target.savings) {
                relaxed -= event.savings;
                event.kind = Event::ONEBYTE;
              }
            }
          }
        }
        assert(optimal >= event.optimal);
        assert(relaxed <= event.relaxed);
        event.optimal = optimal;
        event.relaxed = relaxed;
      }
    } while (relaxed - optimal != prior);
    dst = src = out;
    for (size_t i = 0, e = events.size(); i != e; ++i) {
      auto& event = events[i];
      if (src == dst)
        src = dst = out + event.optimal;
      else
        for (auto e = out + event.optimal; dst != e;)
          *dst++ = *src++;
      const auto& target = events[event.target];
      auto delta = (int)target.optimal - (int)event.optimal;
      if (event.kind == Event::LABEL)
        continue;
      if (event.kind > Event::LABEL) {
        ((int*)dst)[-1] = (int)delta;
        continue;
      }
      assert(delta == (char)delta);
      if (event.savings == 3)
        dst[-2] = 0xeb;
      else
        dst[-2] = dst[-1] - 0x10;
      dst[-1] = (char)delta;
      src += event.savings;
    }
    for (auto e = out + optimal + postfix; dst != e;)
      *dst++ = *src++;
    return dst;
  }

//#include "ops.h"

private:
  byte* ErrorIndexOutOfRange(byte* out, unsigned index) const {
    fprintf(stderr, "Label index of %u is out of range [0, %Iu)\n", index, label_offsets.size());
    return out;
  }

  byte* ErrorCantEncodeDelta(byte* out, ptrdiff_t delta) const {
    fprintf(stderr, "Can't encode delta (%I) with a 32-bit immediate.", delta);
    return out;
  }

  byte* ErrorBinaryTooLarge(byte* out) const {
    fprintf(stderr, "Binary must be less than 2^32 bytes.");
    return out;
  }

  byte* ErrorTrailingLabel(byte* out) const {
    fprintf(stderr, "Instruction stream cannot end with a label.");
    return out;
  }

  std::vector<Instr> stream;
  std::vector<size_t> label_offsets;
};
