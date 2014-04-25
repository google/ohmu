// Warren Hunt : 2013-12-24
#pragma once

#ifdef _MSC_VER
#pragma warning(disable : 4201) // nonstandard extension used : nameless struct/union
#endif

struct __declspec(align(16)) Instr {
	typedef unsigned char byte;

	enum SegmentEncoding { FS_ENCODING = 2, GS_ENCODING };
	enum LockRepEncoding { LOCK_ENCODING = 1, REPZ_ENCODING, REPNZ_ENCODING };

	__forceinline Instr() {}
	__forceinline Instr(unsigned long long instr, int imm32, int disp32)
		: instr(instr), imm32(imm32), disp32(disp32) {}

	byte* Encode(byte* p) const;

	union {
		struct {
			byte code_map    : 4;
			byte invalid     : 1;
			byte long_vex    : 3;

			byte align_pad   : 4;
			byte imm_payload : 1;
			byte             : 3;

			byte imm_size    : 2;
			byte has_imm     : 1;
			byte rip_addr    : 1;
			byte has_modrm   : 1;
			byte has_sib     : 1;
			byte fixed_base  : 1;
			byte force_disp  : 1;

			byte lock_rep    : 2;
			byte size_prefix : 1;
			byte addr_prefix : 1;
			byte use_vex     : 1;
			byte use_rex     : 1;
			byte segment     : 2;

			byte b           : 1;
			byte x           : 1;
			byte r           : 1;
			byte w           : 1;
			byte             : 2;
			byte rex_1       : 1;
			byte             : 1;

			byte simd_prefix : 2;
			byte l           : 1;
			byte vvvv        : 4;
			byte e           : 1;

			byte rm          : 3;
			byte reg         : 3;
			byte mod         : 2;

			byte base        : 3;
			byte index       : 3;
			byte scale       : 2;
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

inline Instr::byte* Instr::Encode(byte* p) const {
	if (invalid) {
		if (imm_payload) goto ENCODE_NO_DISP;
		return p;
	}
	if (prefix) {
		if (prefix & 0xcf) {
			if (segment) *p++ = segment ^ 0x66;
			if (lock_rep) *p++ = lock_rep ^ 0xf1;
			if (size_prefix) *p++ = 0x66;
			if (addr_prefix) *p++ = 0x67;
		}
		if (use_vex) {
			byte rxb = rex << 5;
			if (!long_vex) {
				*p++ = 0xc5;
				*p++ = rxb ^ vex2 ^ 0x80;
				goto ENCODE_OPCODE;
			}
			*p++ = 0xc4;
			*p++ = rxb ^ vex1;
			*p++ = vex2;
			goto ENCODE_OPCODE;
		}
		if (use_rex) *p++ = rex;
	}
	if (code_map) {
		*p++ = 0x0f;
		if (code_map & 0x02)
			*p++ = (vex1 ^ 0xfe) << 1; // high bits of vex1 are 0
	}
ENCODE_OPCODE:
	*p++ = opcode;
	if (!has_modrm) goto ENCODE_NO_DISP;
	byte* pmod = p;
	*p++ = modrm;
	if (mod) goto ENCODE_NO_DISP;
	if (has_sib) *p++ = sib;
	int disp = disp32;
	if (fixed_base) goto ENCODE_DISP32;
	if (disp == 0 && !force_disp) goto ENCODE_NO_DISP;
	if ((char)disp == disp) {
		*pmod |= 0x40;
		*p++ = (byte)disp;
		goto ENCODE_NO_DISP;
	}
	*pmod |= 0x80;
ENCODE_DISP32:
	*(int*)p = disp;
	p += 4;
ENCODE_NO_DISP:
	if (has_imm) {
		if      (imm_size == 0) { *p++ = (byte)imm32; }
		else if (imm_size == 2) { *(int*)p = imm32; p += 4; }
		else if (imm_size <  2) { *(short*)p = (short)imm32; p += 2; }
		else                    { *(long long*)p = *(long long*)&imm32; p += 8; }
	}
	return p;
}
