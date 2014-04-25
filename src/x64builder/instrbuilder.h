// Warren Hunt : 2014/02/08

#include "instr.h"
#include "args.h"

struct InstrBuilder : Instr {
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
	InstrBuilder& SetSegment(SegmentReg a) { segment = a; return *this; }
	InstrBuilder& SetAddressSizeOverride(AddressSizeOverride a) { addr_prefix = a; return *this; }
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
			} else if ((a & 0xff) == 0x3a) {
				code_map = 3;
				a >>= 8;
			} else
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
