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
    struct csr {
        u32 value{0};

        csr() {}
        csr(u32 v) : value{v} {}

        csr(const csr&) = delete;
        csr& operator=(const csr&) = delete;

        csr& operator=(u32 v) {
            value = v;
            return *this;
        }

        operator u32() const {
            return value;
        }

        csr& operator&=(u32 v) {
            value &= v;
            return *this;
        }

        csr& operator|=(u32 v) {
            value |= v;
            return *this;
        }

        template <int left, int right = left>
        struct field_ref {
            u32* p;

            field_ref& operator=(u32 v) {
                *p = set_part(*p, left, right, v);
                return *this;
            }
            operator u32() const { return get_part(*p, left, right); }
        };

        template <int left, int right = left>
        field_ref<left, right> field() { return {&value}; }
    };

    #define CSR_FIELD(name, bit) auto name() { return field<bit>(); }
    #define CSR_FIELD_R(name, left, right) auto name() { return field<left, right>(); }

    struct csr_mstatus : csr {
        using csr::operator=;
        using csr::operator u32;

        CSR_FIELD   (SIE  , 1)
        CSR_FIELD   (MIE  , 3)
        CSR_FIELD   (SPIE , 5)
        CSR_FIELD   (MPIE , 7)
        CSR_FIELD   (SPP  , 8)
        CSR_FIELD_R (MPP  , 12, 11)
        CSR_FIELD   (MPRV , 17)
        CSR_FIELD   (SUM  , 18)
        CSR_FIELD   (MXR  , 19)
        CSR_FIELD   (TVM  , 20)
        CSR_FIELD   (TW   , 21)
        CSR_FIELD   (TSR  , 22)
    };

    struct csr_mip : csr {
        using csr::operator=;
        using csr::operator u32;

        CSR_FIELD (SSIP   , 1)
        CSR_FIELD (MSIP   , 3)
        CSR_FIELD (STIP   , 5)
        CSR_FIELD (MTIP   , 7)
        CSR_FIELD (SEIP   , 9)
        CSR_FIELD (MEIP   , 11)
        CSR_FIELD (LCOFIP , 13)
    };

    struct csr_mie : csr {
        using csr::operator=;
        using csr::operator u32;

        CSR_FIELD (SSIE   , 1)
        CSR_FIELD (MSIE   , 3)
        CSR_FIELD (STIE   , 5)
        CSR_FIELD (MTIE   , 7)
        CSR_FIELD (SEIE   , 9)
        CSR_FIELD (MEIE   , 11)
        CSR_FIELD (LCOFIE , 13)
    };

    #undef CSR_FIELD
    #undef CSR_FIELD_R

public:
    csr         misa;
    csr         mvendorid;
    csr         marchid;
    csr         mimpid;
    csr         mhartid;
    csr_mstatus mstatus;
    csr         mstatush;
    csr         mtvec;
    csr         medeleg;
    csr         mideleg;
    csr_mip     mip;
    csr_mie     mie;
    csr         mscratch;
    csr         mepc;
    csr         mcause;
    csr         mtval;
    csr         mconfigptr;

    // TODO: how to update s mode CSRs when m mode CSRs modified
    csr         sstatus;
    csr         stvec;
    csr         sip;
    csr         sie;
    csr         sscratch;
    csr         sepc;
    csr         scause;
    csr         stval;
    csr         satp;

    u32 pc = 0;
    u32 regs[32] = {0};
    u32 priv = 0b11;

private:
    mmio* mmio_ = 0;

    bool reservation_set = false;
    u32 reserved_addr = 0;

    void csr_rw(u32 csr, u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);

    enum class access_type_t : u32 {r = 0b001, w = 0b010, x = 0b100};

    enum class trap_cause_t : u32 {
        instruction_address_misaligned_exception       = 0,
        instruction_access_fault_exception             = 1,
        illegal_instruction_exception                  = 2,
        breakpoint_exception                           = 3,
        load_address_misaligned_exception              = 4,
        load_access_fault_exception                    = 5,
        store_amo_address_misaligned_exception         = 6,
        store_amo_access_fault_exception               = 7,
        environment_call_from_u_mode_exception         = 8,
        environment_call_from_s_mode_exception         = 9,
        environment_call_from_m_mode_exception         = 11,
        instruction_page_fault_exception               = 12,
        load_page_fault_exception                      = 13,
        store_amo_page_fault_exception                 = 15,

        supervisor_software_interrupt                  = 1  | (1U << 31),
        machine_software_interrupt                     = 3  | (1U << 31),
        supervisor_timer_interrupt                     = 5  | (1U << 31),
        machine_timer_interrupt                        = 7  | (1U << 31),
        supervisor_external_interrupt                  = 9  | (1U << 31),
        machine_external_interrupt                     = 11 | (1U << 31)
    };

    struct trap {
        trap_cause_t cause;
        u32 value;
    };

    trap_cause_t access_type_to_access_fault_exception(access_type_t access_type);
    trap_cause_t access_type_to_page_fault_exception(access_type_t access_type);

    void sv32_ptw(u32 va, u32& pa, access_type_t access_type);

    void load(u32 addr, u32 len, u8* data, access_type_t access_type = access_type_t::r);
    void store(u32 addr, u32 len, const u8* data, access_type_t access_type = access_type_t::w);

    ////

    u32 opcode;
    u32 inst;
    i32 imm;
    u32 rd;
    u32 rs1;
    u32 rs2;
    u32 funct3;
    u32 funct7;

private:
    void decode_lui();
    void decode_auipc();
    void decode_jal();
    void decode_jalr();
    void decode_branch();
    void decode_load();
    void decode_store();
    void decode_imm();
    void decode_op();
    void decode_misc_mem();
    void decode_system();
    void decode_amo();

private:
    void csr_rw_misa          (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void csr_rw_mvendorid     (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void csr_rw_marchid       (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void csr_rw_mimpid        (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void csr_rw_mhartid       (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void csr_rw_mstatus       (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void csr_rw_mstatush      (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void csr_rw_mconfigptr    (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void csr_rw_mtvec         (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void csr_rw_mip           (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void csr_rw_mie           (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void csr_rw_mscratch      (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void csr_rw_mepc          (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void csr_rw_mcause        (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);
    void csr_rw_mtval         (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data);

private:
    void inst_lui       ();
    void inst_auipc     ();
    void inst_jal       ();
    void inst_jalr      ();

    void inst_beq       ();
    void inst_bne       ();
    void inst_blt       ();
    void inst_bge       ();
    void inst_bltu      ();
    void inst_bgeu      ();

    void inst_lb        ();
    void inst_lh        ();
    void inst_lw        ();
    void inst_lbu       ();
    void inst_lhu       ();

    void inst_sb        ();
    void inst_sh        ();
    void inst_sw        ();

    void inst_addi      ();
    void inst_slti      ();
    void inst_sltiu     ();
    void inst_xori      ();
    void inst_ori       ();
    void inst_andi      ();
    void inst_slli      ();
    void inst_srli      ();
    void inst_srai      ();

    void inst_add       ();
    void inst_sub       ();
    void inst_sll       ();
    void inst_slt       ();
    void inst_sltu      ();
    void inst_xor       ();
    void inst_srl       ();
    void inst_sra       ();
    void inst_or        ();
    void inst_and       ();
    void inst_mul       ();
    void inst_mulh      ();
    void inst_mulhsu    ();
    void inst_mulhu     ();
    void inst_div       ();
    void inst_divu      ();
    void inst_rem       ();
    void inst_remu      ();

    void inst_fence     ();
    void inst_fence_i   ();

    void inst_ecall     ();
    void inst_ebreak    ();
    void inst_wfi       ();
    void inst_mret      ();

    void inst_csrrw     ();
    void inst_csrrs     ();
    void inst_csrrc     ();
    void inst_csrrwi    ();
    void inst_csrrsi    ();
    void inst_csrrci    ();

    void inst_lr_w      ();
    void inst_sc_w      ();

    void inst_amoswap_w ();
    void inst_amoadd_w  ();
    void inst_amoxor_w  ();
    void inst_amoand_w  ();
    void inst_amoor_w   ();
    void inst_amomin_w  ();
    void inst_amomax_w  ();
    void inst_amominu_w ();
    void inst_amomaxu_w ();

};