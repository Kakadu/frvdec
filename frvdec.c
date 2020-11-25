
#include "frvdec.h"

#include <stddef.h>
#include <stdint.h>

#define LOAD_LE_1(buf) ((size_t) *(uint8_t*) (buf))
#define LOAD_LE_2(buf) (LOAD_LE_1(buf) | LOAD_LE_1((uint8_t*) (buf) + 1)<<8)
#define LOAD_LE_4(buf) (LOAD_LE_2(buf) | LOAD_LE_2((uint8_t*) (buf) + 2)<<16)
#define UBFX(val, start, end) (((val) >> start) & ((1 << (end - start + 1)) - 1))
#define UNLIKELY(x) __builtin_expect((x), 0)

enum {
  ENC_F_RD  = 1 << 0,
  ENC_F_RS1 = 1 << 1,
  ENC_F_RS2 = 1 << 2,
  ENC_F_RS3 = 1 << 3,
  ENC_F_IMM_MASK = 7 << 4, // 3 bits
  ENC_F_IMM_U = 1 << 4,
  ENC_F_IMM_I = 2 << 4,
  ENC_F_IMM_S = 3 << 4,
  ENC_F_IMM_J = 4 << 4,
  ENC_F_IMM_B = 5 << 4,
  ENC_F_IMM_SHAMT = 6 << 4,
  ENC_F_IMM_AMO = 7 << 4,

  ENC_R = ENC_F_RD | ENC_F_RS1 | ENC_F_RS2,
  ENC_I = ENC_F_RD | ENC_F_RS1 | ENC_F_IMM_I,
  ENC_I_SHAMT = ENC_F_RD | ENC_F_RS1 | ENC_F_IMM_SHAMT,
  ENC_S = ENC_F_RS1 | ENC_F_RS2 | ENC_F_IMM_S,
  ENC_B = ENC_F_RS1 | ENC_F_RS2 | ENC_F_IMM_B,
  ENC_U = ENC_F_RD | ENC_F_IMM_U,
  ENC_J = ENC_F_RD | ENC_F_IMM_J,
};

int frv_decode(size_t bufsz, const uint8_t buf[bufsz], FrvInst* restrict frv_inst) {
  if (UNLIKELY(bufsz < 2))
    return FRV_PARTIAL;
  if ((buf[0] & 0x03) != 0x03)
    return FRV_UNDEF; // 16-bit compressed instructions
  if ((buf[0] & 0x1c) == 0x1c)
    return FRV_UNDEF; // instruction length > 32 bit
  if (UNLIKELY(bufsz < 4))
    return FRV_PARTIAL;

  uint32_t inst = LOAD_LE_4(buf);
  unsigned opcode = UBFX(inst, 2, 6);
  unsigned funct3 = UBFX(inst, 12, 14);
  unsigned funct7 = UBFX(inst, 25, 31);
  unsigned mnem = 0, encoding = 0;
  switch (opcode) {
  case 0x00: encoding = ENC_I;
    mnem = (const uint16_t[]) {FRV_LB, FRV_LH, FRV_LW, FRV_LD, FRV_LBU, FRV_LHU, FRV_LWU, 0}[funct3];
    break;
  case 0x03: encoding = ENC_I;
    mnem = (const uint16_t[]) {FRV_FENCE, FRV_FENCEI, 0, 0, 0, 0, 0, 0}[funct3];
    break;
  case 0x04:
    switch (funct3) {
    case 0: encoding = ENC_I; mnem = FRV_ADDI; break;
    case 1: encoding = ENC_I_SHAMT; mnem = FRV_SLLI; break;
    case 2: encoding = ENC_I; mnem = FRV_SLTI; break;
    case 3: encoding = ENC_I; mnem = FRV_SLTIU; break;
    case 4: encoding = ENC_I; mnem = FRV_XORI; break;
    case 5: encoding = ENC_I_SHAMT; mnem = funct7 & 0x20 ? FRV_SRAI : FRV_SRLI; break;
    case 6: encoding = ENC_I; mnem = FRV_ORI; break;
    case 7: encoding = ENC_I; mnem = FRV_ANDI; break;
    }
    break;
  case 0x05: encoding = ENC_U; mnem = FRV_AUIPC; break;
  case 0x06:
    switch (funct3) {
    case 0: encoding = ENC_I; mnem = FRV_ADDIW; break;
    case 1: encoding = ENC_I_SHAMT; mnem = FRV_SLLIW; break;
    case 5: encoding = ENC_I_SHAMT; mnem = funct7 & 0x20 ? FRV_SRAIW : FRV_SRLIW; break;
    default: return FRV_UNDEF;
    }
    break;
  case 0x08: encoding = ENC_S;
    mnem = (const uint16_t[]) {FRV_SB, FRV_SH, FRV_SW, FRV_SD, 0, 0, 0, 0}[funct3];
    break;
  case 0x0b: encoding = ENC_R | ENC_F_IMM_AMO;
    switch (funct7 >> 2) {
    case 0x00: mnem = (const uint16_t[]) {0, 0, FRV_AMOADDW, FRV_AMOADDD, 0, 0, 0, 0}[funct3]; break;
    case 0x01: mnem = (const uint16_t[]) {0, 0, FRV_AMOSWAPW, FRV_AMOSWAPD, 0, 0, 0, 0}[funct3]; break;
    case 0x02: mnem = (const uint16_t[]) {0, 0, FRV_LRW, FRV_LRD, 0, 0, 0, 0}[funct3]; break;
    case 0x03: mnem = (const uint16_t[]) {0, 0, FRV_SCW, FRV_SCD, 0, 0, 0, 0}[funct3]; break;
    case 0x04: mnem = (const uint16_t[]) {0, 0, FRV_AMOXORW, FRV_AMOXORD, 0, 0, 0, 0}[funct3]; break;
    case 0x08: mnem = (const uint16_t[]) {0, 0, FRV_AMOORW, FRV_AMOORD, 0, 0, 0, 0}[funct3]; break;
    case 0x0c: mnem = (const uint16_t[]) {0, 0, FRV_AMOANDW, FRV_AMOANDD, 0, 0, 0, 0}[funct3]; break;
    case 0x10: mnem = (const uint16_t[]) {0, 0, FRV_AMOMINW, FRV_AMOMIND, 0, 0, 0, 0}[funct3]; break;
    case 0x14: mnem = (const uint16_t[]) {0, 0, FRV_AMOMAXW, FRV_AMOMAXD, 0, 0, 0, 0}[funct3]; break;
    case 0x18: mnem = (const uint16_t[]) {0, 0, FRV_AMOMINUW, FRV_AMOMINUD, 0, 0, 0, 0}[funct3]; break;
    case 0x1c: mnem = (const uint16_t[]) {0, 0, FRV_AMOMAXUW, FRV_AMOMAXUD, 0, 0, 0, 0}[funct3]; break;
    default: return FRV_UNDEF;
    }
    break;
  case 0x0c: encoding = ENC_R;
    switch (funct7) {
    case 0x00: mnem = (const uint16_t[]) {FRV_ADD, FRV_SLL, FRV_SLT, FRV_SLTU, FRV_XOR, FRV_SRL, FRV_OR, FRV_AND}[funct3]; break;
    case 0x01: mnem = (const uint16_t[]) {FRV_MUL, FRV_MULH, FRV_MULHSU, FRV_MULHU, FRV_DIV, FRV_DIVU, FRV_REM, FRV_REMU}[funct3]; break;
    case 0x20: mnem = (const uint16_t[]) {FRV_SUB, 0, 0, 0, 0, FRV_SRA, 0, 0}[funct3]; break;
    default: return FRV_UNDEF;
    }
    break;
  case 0x0d: encoding = ENC_U; mnem = FRV_LUI; break;
  case 0x0e: encoding = ENC_R;
    switch (funct7) {
    case 0x00: mnem = (const uint16_t[]) {FRV_ADDW, FRV_SLLW, 0, 0, 0, FRV_SRLW, 0, 0}[funct3]; break;
    case 0x01: mnem = (const uint16_t[]) {FRV_MULW, 0, 0, 0, FRV_DIVW, FRV_DIVUW, FRV_REMW, FRV_REMUW}[funct3]; break;
    case 0x20: mnem = (const uint16_t[]) {FRV_SUBW, 0, 0, 0, 0, FRV_SRAW, 0, 0}[funct3]; break;
    default: return FRV_UNDEF;
    }
    break;
  case 0x18: encoding = ENC_B;
    mnem = (const uint16_t[]) {FRV_BEQ, FRV_BNE, 0, 0, FRV_BLT, FRV_BGE, FRV_BLTU, FRV_BGEU}[funct3];
    break;
  case 0x19: encoding = ENC_I; mnem = FRV_JALR; break; // TODO: check funct3
  case 0x1b: encoding = ENC_J; mnem = FRV_JAL; break;
  case 0x1c: encoding = ENC_I;
    mnem = (const uint16_t[]) {FRV_ECALL, FRV_CSRRW, FRV_CSRRS, FRV_CSRRC, 0, FRV_CSRRWI, FRV_CSRRSI, FRV_CSRRCI}[funct3];
    break;
  }

  if (!mnem)
    return FRV_UNDEF;
  frv_inst->mnem = mnem;
  frv_inst->rd = (encoding & ENC_F_RD) ? UBFX(inst, 7, 11) : FRV_REG_INV;
  frv_inst->rs1 = (encoding & ENC_F_RS1) ? UBFX(inst, 15, 19) : FRV_REG_INV;
  frv_inst->rs2 = (encoding & ENC_F_RS2) ? UBFX(inst, 20, 24) : FRV_REG_INV;
  frv_inst->rs3 = (encoding & ENC_F_RS3) ? UBFX(inst, 27, 31) : FRV_REG_INV;
  switch (encoding & ENC_F_IMM_MASK) {
  default: frv_inst->imm = 0; break;
  case ENC_F_IMM_U: frv_inst->imm = UBFX(inst, 12, 31) << 12; break;
  case ENC_F_IMM_I: frv_inst->imm = (int32_t) inst >> 20; break;
  case ENC_F_IMM_S: frv_inst->imm = ((int32_t) inst >> 20 & ~0x1f) | UBFX(inst, 7, 11); break;
  case ENC_F_IMM_J:
    frv_inst->imm = (inst & 0xff000) | (inst >> (20-11) & (1 << 11)) |
                    (inst >> 11 & (1 << 20)) |
                    ((int32_t) inst >> (30 - 10) & 0xffe007fe);
    break;
  case ENC_F_IMM_B:
    frv_inst->imm = ((int32_t) inst >> (31-12) & 0xfffff000) |
                    (inst << (11-7) & (1 << 11)) | (inst >> (30-10) & 0x7e0) |
                    (inst >> (11 - 4) & 0x1e);
    break;
  case ENC_F_IMM_SHAMT: frv_inst->imm = UBFX(inst, 20, 25); break;
  case ENC_F_IMM_AMO: frv_inst->imm = UBFX(inst, 25, 26); break;
  }

  return 4;
}

static void strlcat(char* restrict dst, const char* src, size_t size) {
  while (*dst && size)
    dst++, size--;
  while (*src && size > 1)
    *dst++ = *src++, size--;
  if (size)
    *dst = 0;
}

static char* frv_format_int(int32_t val, char buf[static 16]) {
  int32_t absneg = val < 0 ? val : -val; // avoid overflow
  unsigned idx = 16;
  buf[--idx] = 0;
  do {
    buf[--idx] = '0' - (absneg % 10);
    absneg /= 10;
  } while (absneg);
  if (val < 0)
    buf[--idx] = '-';
  return &buf[idx];
}

void frv_format(const FrvInst* inst, size_t len, char buf[len]) {
  char tmp[18];
  if (!len)
    return;
  buf[0] = 0;
  static const char* mnem_str[] = {
    [FRV_LB] = "lb", [FRV_LH] = "lh", [FRV_LW] = "lw", [FRV_LD] = "ld",
    [FRV_LBU] = "lbu", [FRV_LHU] = "lhu", [FRV_LWU] = "lwu",
    [FRV_SB] = "sb", [FRV_SH] = "sh", [FRV_SW] = "sw", [FRV_SD] = "sd",

    [FRV_ADDI] = "addi", [FRV_SLLI] = "slli", [FRV_SLTI] = "slti",
    [FRV_SLTIU] = "sltiu", [FRV_XORI] = "xori", [FRV_SRAI] = "srai",
    [FRV_SRLI] = "srli", [FRV_ORI] = "ori", [FRV_ANDI] = "andi",
    [FRV_ADD] = "add", [FRV_SLL] = "sll", [FRV_SLT] = "slt",
    [FRV_SLTU] = "sltu", [FRV_XOR] = "xor", [FRV_SRL] = "srl", [FRV_OR] = "or",
    [FRV_AND] = "and", [FRV_SUB] = "sub", [FRV_SRA] = "sra",

    [FRV_FENCE] = "fence", [FRV_FENCEI] = "fencei",
    [FRV_AUIPC] = "auipc", [FRV_LUI] = "lui",
    [FRV_JAL] = "jal", [FRV_JALR] = "jalr",
    [FRV_BEQ] = "beq", [FRV_BNE] = "bne", [FRV_BLT] = "blt", [FRV_BGE] = "bge",
    [FRV_BLTU] = "bltu", [FRV_BGEU] = "bgeu",
    [FRV_ECALL] = "ecall",

    [FRV_ADDIW] = "addiw", [FRV_SLLIW] = "slliw", [FRV_SRAIW] = "sraiw",
    [FRV_SRLIW] = "srliw", [FRV_ADDW] = "addw", [FRV_SLLW] = "sllw",
    [FRV_SRLW] = "srlw", [FRV_SUBW] = "subw", [FRV_SRAW] = "sraw",

    [FRV_MUL] = "mul", [FRV_MULH] = "mulh", [FRV_MULHSU] = "mulhsu",
    [FRV_MULHU] = "mulhu", [FRV_DIV] = "div", [FRV_DIVU] = "divu",
    [FRV_REM] = "rem", [FRV_REMU] = "remu", [FRV_MULW] = "mulw",
    [FRV_DIVW] = "divw", [FRV_DIVUW] = "divuw", [FRV_REMW] = "remw",
    [FRV_REMUW] = "remuw",

    [FRV_LRW] = "lr.w", [FRV_SCW] = "sc.w",
    [FRV_LRD] = "lr.d", [FRV_SCD] = "sc.d",
    [FRV_AMOADDW] = "amoadd.w", [FRV_AMOADDD] = "amoadd.d",
    [FRV_AMOSWAPW] = "amoswap.w", [FRV_AMOSWAPD] = "amoswap.d",
    [FRV_AMOXORW] = "amoxor.w", [FRV_AMOXORD] = "amoxor.d",
    [FRV_AMOORW] = "amoor.w", [FRV_AMOORD] = "amoor.d",
    [FRV_AMOANDW] = "amoand.w", [FRV_AMOANDD] = "amoand.d",
    [FRV_AMOMINW] = "amomin.w", [FRV_AMOMIND] = "amomin.d",
    [FRV_AMOMAXW] = "amomax.w", [FRV_AMOMAXD] = "amomax.d",
    [FRV_AMOMINUW] = "amominu.w", [FRV_AMOMINUD] = "amominu.d",
    [FRV_AMOMAXUW] = "amomaxu.w", [FRV_AMOMAXUD] = "amomaxu.d",

    [FRV_CSRRW] = "csrrw", [FRV_CSRRWI] = "csrrwi",
    [FRV_CSRRS] = "csrrs", [FRV_CSRRSI] = "csrrsi",
    [FRV_CSRRC] = "csrrc", [FRV_CSRRCI] = "csrrci",
  };
  if (inst->mnem >= sizeof mnem_str / sizeof mnem_str[0] || !mnem_str[inst->mnem]) {
    strlcat(buf, "<invalid>", len);
    return;
  }
  strlcat(buf, mnem_str[inst->mnem], len);
  if (inst->rd != FRV_REG_INV) {
    char* fmt = frv_format_int(inst->rd, tmp + 2);
    *--fmt = 'r';
    *--fmt = ' ';
    strlcat(buf, fmt, len);
  }
  if (inst->rs1 != FRV_REG_INV) {
    char* fmt = frv_format_int(inst->rs1, tmp + 2);
    *--fmt = 'r';
    *--fmt = ' ';
    strlcat(buf, fmt, len);
  }
  if (inst->rs2 != FRV_REG_INV) {
    char* fmt = frv_format_int(inst->rs2, tmp + 2);
    *--fmt = 'r';
    *--fmt = ' ';
    strlcat(buf, fmt, len);
  }
  if (inst->rs3 != FRV_REG_INV) {
    char* fmt = frv_format_int(inst->rs3, tmp + 2);
    *--fmt = 'r';
    *--fmt = ' ';
    strlcat(buf, fmt, len);
  }
  if (inst->imm) {
    char* fmt = frv_format_int(inst->imm, tmp + 2);
    *--fmt = ' ';
    strlcat(buf, fmt, len);
  }
}