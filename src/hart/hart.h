#pragma once

#include "common.h"
#include "mmio.h"
#include "constants.h"

class hart {

public:
    hart(mmio* mmio, u32 mhartid, u32 pc);

    void set_meip(bool level);
    void set_seip(bool level);

    void set_mtip(bool level);
    void set_stip(bool level);

    void set_msip(bool level);
    void set_ssip(bool level);

    void step();

public:
    u32 misa       = 0;
    u32 mvendorid  = 0;
    u32 marchid    = 0;
    u32 mimpid     = 0;
    u32 mhartid;
    u32 mstatus;
    u32 mstatush   = 0;
    u32 mconfigptr = 0;
    u32 mtvec      = 0;
    u32 mip        = 0;
    u32 mie        = 0;
    u32 mscratch   = 0;
    u32 mepc       = 0;
    u32 mcause     = 0;
    u32 mtval      = 0;

    u32 pc = 0;
    u32 regs[32] = {0};
    u32 priv = 0b11;

private:
    mmio* mmio_ = 0;

    bool reservation_set = false;
    u32 reserved_addr = 0;

    bool csr_rw(u32 csr, u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void load(u32 addr, u32 len, u8* data, u32 exc = MCAUSE_LOAD_ACCESS_FAULT_EXCEPTION);
    void store(u32 addr, u32 len, const u8* data, u32 exc = MCAUSE_STORE_AMO_ACCESS_FAULT_EXCEPTION);

    struct trap {
        u32 cause;
        u32 tval;
    };

    ////

    u32 opcode;
    u32 inst;
    i32 imm;
    u32 rd;
    u32 rs1;
    u32 rs2;
    u32 funct3;
    u32 funct7;

    void op_lui();
    void op_auipc();
    void op_jal();
    void op_jalr();
    void op_branch();
    void op_load();
    void op_store();
    void op_imm();
    void op_op();
    void op_misc_mem();
    void op_system();
    void op_amo();

private:
    bool csr_rw_misa          (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    bool csr_rw_mvendorid     (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    bool csr_rw_marchid       (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    bool csr_rw_mimpid        (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    bool csr_rw_mhartid       (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    bool csr_rw_mstatus       (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    bool csr_rw_mstatush      (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    bool csr_rw_mconfigptr    (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    bool csr_rw_mtvec         (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    bool csr_rw_mip           (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    bool csr_rw_mie           (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    bool csr_rw_mscratch      (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    bool csr_rw_mepc          (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    bool csr_rw_mcause        (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    bool csr_rw_mtval         (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
};