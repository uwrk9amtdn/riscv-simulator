
#include <hart.h>

void hart::decode_amo()
{
    u32 funct5 = get_part(inst, 31, 27);
    // u32 aq = get_part(inst, 26, 26);
    // u32 rl = get_part(inst, 25, 25);

    if (funct3 != FUNCT3_AMO) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }

    switch (funct5) {
        case FUNCT5_LR_W:       return inst_lr_w();
        case FUNCT5_SC_W:       return inst_sc_w();
        case FUNCT5_AMOSWAP_W:  return inst_amoswap_w();
        case FUNCT5_AMOADD_W:   return inst_amoadd_w();
        case FUNCT5_AMOXOR_W:   return inst_amoxor_w();
        case FUNCT5_AMOAND_W:   return inst_amoand_w();
        case FUNCT5_AMOOR_W:    return inst_amoor_w();
        case FUNCT5_AMOMIN_W:   return inst_amomin_w();
        case FUNCT5_AMOMAX_W:   return inst_amomax_w();
        case FUNCT5_AMOMINU_W:  return inst_amominu_w();
        case FUNCT5_AMOMAXU_W:  return inst_amomaxu_w();
    }

    throw trap{trap_cause_t::illegal_instruction_exception, inst};

}

void hart::inst_lr_w() {
    u32 addr;
    u32 ldata;
    if (rs2 != 0) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }

    addr = regs[rs1];

    if (addr & 0x03) {
        throw trap{trap_cause_t::load_address_misaligned_exception, addr};
    }

    load(addr, 4, (u8*)&ldata, access_type_t::w);

    regs[rd] = ldata;

    reservation_set = true;
    reserved_addr = addr;

    pc = pc + 4;
}

void hart::inst_sc_w() {
    u32 addr;
    u32 sdata;

    addr = regs[rs1];
    sdata = regs[rs2];

    if (addr & 0x03) {
        throw trap{trap_cause_t::store_amo_address_misaligned_exception, addr};
    }

    if (reservation_set && reserved_addr == addr) {
        store(addr, 4, (u8*)&sdata);

        regs[rd] = 0;
        reservation_set = false;
    } else {
        regs[rd] = 1;
    }

    pc = pc + 4;
}

void hart::inst_amoswap_w () {
    u32 ldata;
    u32 sdata;
    u32 addr;

    addr = regs[rs1];

    if (addr & 0x03) {
        throw trap{trap_cause_t::store_amo_address_misaligned_exception, addr};
    }

    load(addr, 4, (u8*)&ldata, access_type_t::w);

    sdata = regs[rs2];

    store(addr, 4, (u8*)&sdata);

    regs[rd] = ldata;

    pc = pc + 4;
}

void hart::inst_amoadd_w () {
    u32 ldata;
    u32 sdata;
    u32 addr;

    addr = regs[rs1];

    if (addr & 0x03) {
        throw trap{trap_cause_t::store_amo_address_misaligned_exception, addr};
    }

    load(addr, 4, (u8*)&ldata, access_type_t::w);

    sdata = ldata + regs[rs2];

    store(addr, 4, (u8*)&sdata);

    regs[rd] = ldata;

    pc = pc + 4;
}

void hart::inst_amoxor_w () {
    u32 ldata;
    u32 sdata;
    u32 addr;

    addr = regs[rs1];

    if (addr & 0x03) {
        throw trap{trap_cause_t::store_amo_address_misaligned_exception, addr};
    }

    load(addr, 4, (u8*)&ldata, access_type_t::w);

    sdata = ldata ^ regs[rs2];

    store(addr, 4, (u8*)&sdata);

    regs[rd] = ldata;

    pc = pc + 4;
}

void hart::inst_amoand_w () {
    u32 ldata;
    u32 sdata;
    u32 addr;

    addr = regs[rs1];

    if (addr & 0x03) {
        throw trap{trap_cause_t::store_amo_address_misaligned_exception, addr};
    }

    load(addr, 4, (u8*)&ldata, access_type_t::w);

    sdata = ldata & regs[rs2];

    store(addr, 4, (u8*)&sdata);

    regs[rd] = ldata;

    pc = pc + 4;
}

void hart::inst_amoor_w () {
    u32 ldata;
    u32 sdata;
    u32 addr;

    addr = regs[rs1];

    if (addr & 0x03) {
        throw trap{trap_cause_t::store_amo_address_misaligned_exception, addr};
    }

    load(addr, 4, (u8*)&ldata, access_type_t::w);

    sdata = ldata | regs[rs2];

    store(addr, 4, (u8*)&sdata);

    regs[rd] = ldata;

    pc = pc + 4;
}

void hart::inst_amomin_w () {
    u32 ldata;
    u32 sdata;
    u32 addr;

    addr = regs[rs1];

    if (addr & 0x03) {
        throw trap{trap_cause_t::store_amo_address_misaligned_exception, addr};
    }

    load(addr, 4, (u8*)&ldata, access_type_t::w);

    sdata = ((i32)ldata > (i32)regs[rs2]) ? regs[rs2] : ldata;

    store(addr, 4, (u8*)&sdata);

    regs[rd] = ldata;

    pc = pc + 4;
}

void hart::inst_amomax_w () {
    u32 ldata;
    u32 sdata;
    u32 addr;

    addr = regs[rs1];

    if (addr & 0x03) {
        throw trap{trap_cause_t::store_amo_address_misaligned_exception, addr};
    }

    load(addr, 4, (u8*)&ldata, access_type_t::w);

    sdata = ((i32)ldata > (i32)regs[rs2]) ? ldata : regs[rs2];

    store(addr, 4, (u8*)&sdata);

    regs[rd] = ldata;

    pc = pc + 4;
}

void hart::inst_amominu_w () {
    u32 ldata;
    u32 sdata;
    u32 addr;

    addr = regs[rs1];

    if (addr & 0x03) {
        throw trap{trap_cause_t::store_amo_address_misaligned_exception, addr};
    }

    load(addr, 4, (u8*)&ldata, access_type_t::w);

    sdata = (ldata > regs[rs2]) ? regs[rs2] : ldata;

    store(addr, 4, (u8*)&sdata);

    regs[rd] = ldata;

    pc = pc + 4;
}

void hart::inst_amomaxu_w () {
    u32 ldata;
    u32 sdata;
    u32 addr;

    addr = regs[rs1];

    if (addr & 0x03) {
        throw trap{trap_cause_t::store_amo_address_misaligned_exception, addr};
    }

    load(addr, 4, (u8*)&ldata, access_type_t::w);

    sdata = (ldata > regs[rs2]) ? ldata : regs[rs2];

    store(addr, 4, (u8*)&sdata);

    regs[rd] = ldata;

    pc = pc + 4;
}
