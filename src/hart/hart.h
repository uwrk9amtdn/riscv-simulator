#pragma once

#include "common.h"
#include "mmio.h"
#include "constants.h"
#include "tlb.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <string>

// -DTLB_STATIC=DirectMappedTLB (or another final class) makes the TLB type concrete so lookups inline
#ifdef TLB_STATIC
using hart_tlb_t = TLB_STATIC;
#else
using hart_tlb_t = TLB;
#endif

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
    u32 run(u32 n); // returns the number of steps done, stops early when WFI parks the hart

    // kind: off (no lookup at all), null, dm, sa, fa
    void set_tlb(const std::string& kind, u32 entries, u32 ways);
    const hart_tlb_t* get_itlb() const { return itlb.get(); }
    const hart_tlb_t* get_dtlb() const { return dtlb.get(); }

    // RAM fast path: physical accesses inside [base, base + size) go straight to host memory instead of
    // through the mmio tree. The region must not be shared with any other device
    void set_ram(u32 base, u32 size, u8* host) { ram_base = base; ram_size = size; ram_host = host; }

    struct fast_tlb_stats_t { u64 ifills = 0, dfills = 0, flushes = 0; };
    const fast_tlb_stats_t& get_fast_tlb_stats() const { return fast_stats; }
    u64 get_decodes() const { return decodes; }

    bool waiting = false;

    bool wfi_wake_pending() { return (effective_mip() & mie) != 0; }

    // reserved bit of irq_lines: not an interrupt, never visible in mip. Wakes a sleeping hart later (stop, debugger)
    static constexpr u32 LINE_KICK = 1u << 31;

    // mip as software sees it: the bits the hart writes itself plus the external lines
    u32 effective_mip() const { return mip | (irq_lines.load(std::memory_order_acquire) & ~LINE_KICK); }

public:

    using csr = bitfield;

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

    struct csr_satp : csr {
        using csr::operator=;
        using csr::operator u32;

        CSR_FIELD_R (PPN , 21, 0)
        CSR_FIELD_R (ASID, 30, 22)
        CSR_FIELD   (MODE, 31)
    };

    struct csr_tvec : csr {
        using csr::operator=;
        using csr::operator u32;

        CSR_FIELD_R (BASE , 31, 2)

        CSR_FIELD_R (MODE, 1, 0)
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
    csr_tvec    mtvec;
    csr         medeleg;
    csr         mideleg;
    csr_mip     mip;
    csr_mie     mie;
    csr         mscratch;
    csr         mepc;
    csr         mcause;
    csr         mtval;
    csr         mconfigptr;
    csr         mcounteren;

    csr_tvec    stvec;
    csr         sscratch;
    csr         sepc;
    csr         scause;
    csr         stval;
    csr_satp    satp;
    csr         scounteren;

    u32 pc = 0;
    u32 regs[32] = {0};
    u32 priv = 0b11;

private:
    mmio* mmio_ = 0;

    bool reservation_set = false;
    u32 reserved_addr = 0;

    void csr_rw(u32 csr, u32 rm, u32 wm, u32& rd, u32 wd);

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

    void check_interrupts(); // throws the highest priority pending and enabled interrupt
    void execute();          // fetch, decode and execute one instruction
    void take_trap(const trap& t);

    trap_cause_t access_type_to_access_fault_exception(access_type_t access_type);
    trap_cause_t access_type_to_page_fault_exception(access_type_t access_type);

    void sv32_perm_check(u32 va, u32 u, u32 xwr, access_type_t access_type, u32 priv);

    u32 sv32_ptw(u32 va, access_type_t access_type, u32 priv);

    u32 resolve_addr(u32 addr, access_type_t access_type);

    // Fast TLB in front of resolve_addr + bus: guest VPN -> host pointer for RAM pages, with a tag per
    // access type that is only set after that access passed the full permission check. A hit is
    // compare + add + copy. The entries depend on the translation context (priv, MPRV/MPP, SUM, MXR,
    // satp), so they are flushed when it changes. Loads/stores are aligned, so an access never
    // crosses a page
    static constexpr u32 FAST_TLB_BITS = 8;
    static constexpr u32 FAST_TLB_INVALID = ~0u; // never equal to a 20 bit vpn

    struct fast_i_entry { u32 tag_x; uintptr_t addend; };          // host address = addend + va
    struct fast_d_entry { u32 tag_r; u32 tag_w; uintptr_t addend; };

    fast_i_entry fast_i[1u << FAST_TLB_BITS];
    fast_d_entry fast_d[1u << FAST_TLB_BITS];
    u32 fast_key = FAST_TLB_INVALID;

    // fetch-page cache: the fast_i entry of the page pc is on, so a fetch on the same page is one compare.
    // Cleared by fast_flush, so it is invalidated by every context change, satp change and sfence.vma
    u32 fetch_vpn = FAST_TLB_INVALID;
    uintptr_t fetch_addend = 0;

    void fetch()
    {
        u32 vpn = get_part(pc, 31, 12);
        if (vpn == fetch_vpn) [[likely]] {
            memcpy(&inst, (const u8*)(fetch_addend + pc), 4);
            return;
        }
        load(pc, 4, (u8*)&inst, access_type_t::x);
        const fast_i_entry& e = fast_i[get_part(vpn, FAST_TLB_BITS - 1, 0)];
        if (e.tag_x == vpn) { // the page is RAM and passed the execute check
            fetch_vpn = vpn;
            fetch_addend = e.addend;
        }
    }
    fast_tlb_stats_t fast_stats;

    void fast_flush();
    void fast_check_context(); // flush if priv, data priv (MPRV/MPP), SUM or MXR changed
    void fast_fill(access_type_t access_type, u32 va, u32 pa);

    void load_slow(u32 addr, u32 len, u8* data, access_type_t access_type);
    void store_slow(u32 addr, u32 len, const u8* data, access_type_t access_type);

    void load(u32 addr, u32 len, u8* data, access_type_t access_type = access_type_t::r)
    {
        u32 vpn = get_part(addr, 31, 12);
        u32 i = get_part(vpn, FAST_TLB_BITS - 1, 0);
        if (access_type == access_type_t::x) {
            if (fast_i[i].tag_x == vpn) [[likely]] {
                copy_small(data, (const u8*)(fast_i[i].addend + addr), len);
                return;
            }
        } else {
            u32 tag = access_type == access_type_t::r ? fast_d[i].tag_r : fast_d[i].tag_w;
            if (tag == vpn) [[likely]] {
                copy_small(data, (const u8*)(fast_d[i].addend + addr), len);
                return;
            }
        }
        load_slow(addr, len, data, access_type);
    }

    void store(u32 addr, u32 len, const u8* data, access_type_t access_type = access_type_t::w)
    {
        u32 vpn = get_part(addr, 31, 12);
        u32 i = get_part(vpn, FAST_TLB_BITS - 1, 0);
        if (fast_d[i].tag_w == vpn) [[likely]] {
            copy_small((u8*)(fast_d[i].addend + addr), data, len);
            return;
        }
        store_slow(addr, len, data, access_type);
    }

    // external interrupt lines, driven by devices: MEIP and SEIP (PLIC), MTIP and MSIP (CLINT). Atomic, so
    // with threaded harts another thread can raise them. mip only keeps the bits the hart writes itself
    // (SSIP, STIP, software SEIP), so MEIP/MTIP/MSIP are never set in mip
    std::atomic<u32> irq_lines{0};

    void set_line(u32 bit, bool level);

    // set by everything that can change which interrupt is pending and enabled (set_*ip, CSR access,
    // trap entry, mret/sret). run() only does the full interrupt check when it is set
    bool irq_dirty = true;

    u32 ram_base = 0;
    u32 ram_size = 0;
    u8* ram_host = nullptr;

    // physical bus access: RAM fast path, otherwise the mmio tree. len is 1, 2 or 4
    static inline void copy_small(u8* dst, const u8* src, u32 len)
    {
        switch (len) {
            case 4: memcpy(dst, src, 4); break;
            case 2: memcpy(dst, src, 2); break;
            case 1: *dst = *src; break;
            default: memcpy(dst, src, len); break;
        }
    }

    bool bus_load(u32 pa, u32 len, u8* data)
    {
        u32 off = pa - ram_base; // wraps for pa < ram_base, so one compare covers both bounds
        if (off < ram_size && len <= ram_size - off) [[likely]] {
            copy_small(data, ram_host + off, len);
            return true;
        }
        return mmio_->load(pa, len, data);
    }

    bool bus_store(u32 pa, u32 len, const u8* data)
    {
        u32 off = pa - ram_base;
        if (off < ram_size && len <= ram_size - off) [[likely]] {
            copy_small(ram_host + off, data, len);
            return true;
        }
        return mmio_->store(pa, len, data);
    }

    ////

    u32 opcode;
    u32 inst;
    i32 imm;
    u32 rd;
    u32 rs1;
    u32 rs2;
    u32 funct3;
    u32 funct7;

    std::unique_ptr<hart_tlb_t> itlb;
    std::unique_ptr<hart_tlb_t> dtlb;

private:
    // decoded-instruction cache: a memo of decode(raw), indexed by pc. The decoded form depends only on
    // the 32 bit instruction word, so an entry is valid whenever its raw word equals the fetched one.
    // Self-modifying code, fence.i, other harts writing code and virtual aliases need no invalidation:
    // a changed word just misses and is decoded again
    using handler_t = void (*)(hart&);

    // plain function pointer to a member handler; the handler is inlined into the thunk
    template <void (hart::*F)()>
    static void thunk(hart& h) { (h.*F)(); }

    // 16 bytes: rd/rs1/rs2 are cheap to take from raw again, so they are not stored
    struct decoded {
        u32 raw;
        i32 imm; // sign-extended and reassembled for the instruction's format
        handler_t handler;
    };

    static constexpr u32 DCACHE_BITS = 18;
    std::unique_ptr<decoded[]> dcache;
    u64 decodes = 0;

    void decode_into(decoded& d); // decodes inst

    void inst_illegal();

    handler_t decode_lui();
    handler_t decode_auipc();
    handler_t decode_jal();
    handler_t decode_jalr();
    handler_t decode_branch();
    handler_t decode_load();
    handler_t decode_store();
    handler_t decode_imm();
    handler_t decode_op();
    handler_t decode_misc_mem();
    handler_t decode_system();
    handler_t decode_amo();

private:
    void csr_rw_misa          (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mvendorid     (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_marchid       (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mimpid        (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mhartid       (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mstatus       (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mstatush      (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mconfigptr    (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mtvec         (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mip           (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mie           (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mscratch      (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mepc          (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mcause        (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mtval         (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_medeleg       (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mideleg       (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_mcounteren    (u32 rm, u32 wm, u32& rd, u32 wd);

    void csr_rw_sstatus       (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_sip           (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_sie           (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_stvec         (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_sscratch      (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_sepc          (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_scause        (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_stval         (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_satp          (u32 rm, u32 wm, u32& rd, u32 wd);
    void csr_rw_scounteren    (u32 rm, u32 wm, u32& rd, u32 wd);

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
    void inst_sfence_vma();

    void inst_ecall     ();
    void inst_ebreak    ();
    void inst_wfi       ();
    void inst_mret      ();
    void inst_sret      ();

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