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
#pragma once

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

enum SegmentReg {
	FS = 2,
	GS,
};

enum ConditionCode {
	O, NO, B, NAE = B, C = B, NB, AE = NB, NC = NB, Z, E = Z, NZ, NE = NZ, BE, NA = BE, NBE, A = NBE,
	S, NS, P, PE = P, NP, PO = NP, L, NGE = L, NL, GE = NL, LE, NG = LE, NLE, G = NLE
};

enum AddressSizeOverride{
	ADDRESS_SIZE_OVERRIDE = 1,
};
