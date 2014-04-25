// Warren Hunt : 2014-03-14
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
