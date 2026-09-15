#include "common.h"

// clang-format off
constexpr u32 OP_LUI      = 0b0110111;
constexpr u32 OP_AUIPC    = 0b0010111;
constexpr u32 OP_JAL      = 0b1101111;
constexpr u32 OP_JALR     = 0b1100111;
constexpr u32 OP_BRANCH   = 0b1100011;
constexpr u32 OP_LOAD     = 0b0000011;
constexpr u32 OP_STORE    = 0b0100011;
constexpr u32 OP_IMM      = 0b0010011;
constexpr u32 OP_OP       = 0b0110011;
constexpr u32 OP_MISC_MEM = 0b0001111;
constexpr u32 OP_SYSTEM   = 0b1110011;
constexpr u32 OP_AMO      = 0b0101111;

constexpr u32 MCAUSE_INSTRUCTION_ADDRESS_MISALIGNED_EXCEPTION = 0x00000000;
constexpr u32 MCAUSE_INSTRUCTION_ACCESS_FAULT_EXCEPTION       = 0x00000001;
constexpr u32 MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION            = 0x00000002;
constexpr u32 MCAUSE_BREAKPOINT_EXCEPTION                     = 0x00000003;
constexpr u32 MCAUSE_LOAD_ADDRESS_MISALIGNED_EXCEPTION        = 0x00000004;
constexpr u32 MCAUSE_LOAD_ACCESS_FAULT_EXCEPTION              = 0x00000005;
constexpr u32 MCAUSE_STORE_AMO_ADDRESS_MISALIGNED_EXCEPTION   = 0x00000006;
constexpr u32 MCAUSE_STORE_AMO_ACCESS_FAULT_EXCEPTION         = 0x00000007;
constexpr u32 MCAUSE_ENVIRONMENT_CALL_FROM_U_MODE_EXCEPTION   = 0x00000008;
constexpr u32 MCAUSE_ENVIRONMENT_CALL_FROM_M_MODE_EXCEPTION   = 0x0000000b;

constexpr u32 MCAUSE_MACHINE_SOFTWARE_INTERRUPT = 0x80000003;
constexpr u32 MCAUSE_MACHINE_TIMER_INTERRUPT    = 0x80000007;
constexpr u32 MCAUSE_MACHINE_EXTERNAL_INTERRUPT = 0x8000000b;

constexpr u32 CSR_MVENDORID  = 0xf11;
constexpr u32 CSR_MARCHID    = 0xf12;
constexpr u32 CSR_MIMPID     = 0xf13;
constexpr u32 CSR_MHARTID    = 0xf14;
constexpr u32 CSR_MCONFIGPTR = 0xf15;
constexpr u32 CSR_MSTATUS    = 0x300;
constexpr u32 CSR_MISA       = 0x301;
constexpr u32 CSR_MIE        = 0x304;
constexpr u32 CSR_MTVEC      = 0x305;
constexpr u32 CSR_MSTATUSH   = 0x310;
constexpr u32 CSR_MSCRATCH   = 0x340;
constexpr u32 CSR_MEPC       = 0x341;
constexpr u32 CSR_MCAUSE     = 0x342;
constexpr u32 CSR_MTVAL      = 0x343;
constexpr u32 CSR_MIP        = 0x344;

// branch
constexpr u32 FUNCT3_BEQ  = 0b000;
constexpr u32 FUNCT3_BNE  = 0b001;
constexpr u32 FUNCT3_BLT  = 0b100;
constexpr u32 FUNCT3_BGE  = 0b101;
constexpr u32 FUNCT3_BLTU = 0b110;
constexpr u32 FUNCT3_BGEU = 0b111;

// load
constexpr u32 FUNCT3_LB  = 0b000;
constexpr u32 FUNCT3_LH  = 0b001;
constexpr u32 FUNCT3_LW  = 0b010;
constexpr u32 FUNCT3_LBU = 0b100;
constexpr u32 FUNCT3_LHU = 0b101;

// store
constexpr u32 FUNCT3_SB = 0b000;
constexpr u32 FUNCT3_SH = 0b001;
constexpr u32 FUNCT3_SW = 0b010;

// imm
constexpr u32 FUNCT3_ADDI      = 0b000;
constexpr u32 FUNCT3_SLTI      = 0b010;
constexpr u32 FUNCT3_SLTIU     = 0b011;
constexpr u32 FUNCT3_XORI      = 0b100;
constexpr u32 FUNCT3_ORI       = 0b110;
constexpr u32 FUNCT3_ANDI      = 0b111;
constexpr u32 FUNCT3_SLLI      = 0b001;
constexpr u32 FUNCT3_SRLI_SRAI = 0b101;

constexpr u32 FUNCT7_SLLI = 0b0000000;
constexpr u32 FUNCT7_SRLI = 0b0000000;
constexpr u32 FUNCT7_SRAI = 0b0100000;

constexpr u32 FUNCT10_ADD     = 0b0000000000;
constexpr u32 FUNCT10_SUB     = 0b0100000000;
constexpr u32 FUNCT10_SLL     = 0b0000000001;
constexpr u32 FUNCT10_SLT     = 0b0000000010;
constexpr u32 FUNCT10_SLTU    = 0b0000000011;
constexpr u32 FUNCT10_XOR     = 0b0000000100;
constexpr u32 FUNCT10_SRL     = 0b0000000101;
constexpr u32 FUNCT10_SRA     = 0b0100000101;
constexpr u32 FUNCT10_OR      = 0b0000000110;
constexpr u32 FUNCT10_AND     = 0b0000000111;
constexpr u32 FUNCT10_MUL     = 0b0000001000;
constexpr u32 FUNCT10_MULH    = 0b0000001001;
constexpr u32 FUNCT10_MULHSU  = 0b0000001010;
constexpr u32 FUNCT10_MULHU   = 0b0000001011;
constexpr u32 FUNCT10_DIV     = 0b0000001100;
constexpr u32 FUNCT10_DIVU    = 0b0000001101;
constexpr u32 FUNCT10_REM     = 0b0000001110;
constexpr u32 FUNCT10_REMU    = 0b0000001111;

constexpr u32 FUNCT7_ECALL_EBREAK = 0b0000000;
constexpr u32 FUNCT7_WFI          = 0b0001000;
constexpr u32 FUNCT7_MRET         = 0b0011000;

constexpr u32 RS2_ECALL  = 0b00000;
constexpr u32 RS2_EBREAK = 0b00001;
constexpr u32 RS2_WFI    = 0b00101;
constexpr u32 RS2_MRET   = 0b00010;

constexpr u32 FUNCT3_ECALL_EBREAK_WFI_MRET = 0b000;
constexpr u32 FUNCT3_CSRRW                 = 0b001;
constexpr u32 FUNCT3_CSRRS                 = 0b010;
constexpr u32 FUNCT3_CSRRC                 = 0b011;
constexpr u32 FUNCT3_CSRRWI                = 0b101;
constexpr u32 FUNCT3_CSRRSI                = 0b110;
constexpr u32 FUNCT3_CSRRCI                = 0b111;

constexpr u32 FUNCT3_FENCE   = 0b000;
constexpr u32 FUNCT3_FENCE_I = 0b001;

constexpr u32 FUNCT3_AMO = 0b010;

constexpr u32 FUNCT5_LR_W      = 0b00010;
constexpr u32 FUNCT5_SC_W      = 0b00011;
constexpr u32 FUNCT5_AMOSWAP_W = 0b00001;
constexpr u32 FUNCT5_AMOADD_W  = 0b00000;
constexpr u32 FUNCT5_AMOXOR_W  = 0b00100;
constexpr u32 FUNCT5_AMOAND_W  = 0b01100;
constexpr u32 FUNCT5_AMOOR_W   = 0b01000;
constexpr u32 FUNCT5_AMOMIN_W  = 0b10000;
constexpr u32 FUNCT5_AMOMAX_W  = 0b10100;
constexpr u32 FUNCT5_AMOMINU_W = 0b11000;
constexpr u32 FUNCT5_AMOMAXU_W = 0b11100;

// clang-format on