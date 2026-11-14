/* SPDX-License-Identifier: MIT - QEMU virt laboratory, one CPU. */
#include <atlantic/entry.h>

#define RSR(name, output) __asm__ volatile("mrs %0, " #name : "=r"(output) : : "memory")
#define WSR(name, val) __asm__ volatile("msr " #name ", %0; isb" : : "r"((uint64_t)(val)) : "memory")
/* QEMU virt, security disabled and a single redistributor. */
#define GICD(off) ((uintptr_t)0x08000000 + (off))
#define GICR(off) ((uintptr_t)0x080a0000 + (off))
#define GICR_SGI(off) GICR(0x10000 + (off))
#define VIRTUAL_TIMER_INTID 27u
#define VIRTUAL_TIMER ((uint32_t)1 << VIRTUAL_TIMER_INTID)

extern char atl_irq_probe_vectors[];
static volatile uint64_t firings, acknowledgements, completions, failures;
static volatile uint64_t observed_id, observed_ctl, after_ctl, vector_pc, irq_now;
static volatile uint64_t active_before, active_after, pending_after;
static volatile uint64_t irq_period;

static uint32_t rd(uintptr_t p) { return *(volatile uint32_t *)p; }
static void wr(uintptr_t p, uint32_t x) { *(volatile uint32_t *)p = x; }
static uint64_t count(void) {
    uint64_t x;
    __asm__ volatile("isb; mrs %0, CNTVCT_EL0" : "=r"(x) : : "memory");
    return x;
}
static void barrier(void) { __asm__ volatile("dsb sy; isb" : : : "memory"); }
static void loghex(const char *key, uint64_t val) {
    static const char digit[] = "0123456789abcdef";
    char out[20] = "0x0000000000000000\n";
    for (unsigned i=0; i<16; ++i) out[17-i] = digit[(val>>(4*i))&15];
    atlantic_early_platform.write(key);
    atlantic_early_platform.write(out);
}
static int fail(const char *msg) {
    __asm__ volatile("msr daifset, #2; isb" : : : "memory");
    WSR(CNTV_CTL_EL0, 0);
    atlantic_early_platform.write(msg);
    return -1;
}
static int mmio_wait(uintptr_t p, uint32_t mask, uint64_t budget) {
    uint64_t start = count();
    while (rd(p) & mask) {
        if (count() - start >= budget) return -1;
    }
    return 0;
}

void atl_irq_probe_handler(uint64_t pc) {
    uint64_t id, ctl;
    RSR(ICC_IAR1_EL1, id);
    id &= 0xffffff;
    uint64_t now = count();
    RSR(CNTV_CTL_EL0, ctl);
    observed_id = id;
    vector_pc = pc;
    irq_now = now;
    observed_ctl = ctl;
    active_before = rd(GICR_SGI(0x300))&VIRTUAL_TIMER;

    /* Mask before moving the comparison into the future. Keep ENABLE set for
       a defined ISTATUS observation; deassert source before completing GIC. */
    WSR(CNTV_CTL_EL0, 3);
    WSR(CNTV_CVAL_EL0, now+irq_period);
    RSR(CNTV_CTL_EL0, ctl);
    after_ctl = ctl;
    barrier();

    if (id < 1020) {
        ++acknowledgements;
        WSR(ICC_EOIR1_EL1, id);
        barrier();
        ++completions;
    }
    active_after = rd(GICR_SGI(0x300))&VIRTUAL_TIMER;
    pending_after = rd(GICR_SGI(0x200))&VIRTUAL_TIMER;
    if (id != VIRTUAL_TIMER_INTID || (observed_ctl&7)!=5 || (after_ctl&7)!=3 ||
        !active_before || active_after || pending_after) ++failures;
    ++firings;
}

int atl_irq_probe(const struct AtlanticEntryCapture *capture) {
    const uint64_t half_second = capture->cntfrq/2;
    if (capture->cntfrq<1000 || capture->cntfrq>1000000000) return fail("FAIL:IRQ_FREQUENCY\n");
    uint64_t sctlr, sre, mode;
    RSR(SCTLR_EL1, sctlr);
    if (sctlr&0x1005) return fail("FAIL:IRQ_MEMORY_REGIME\n");
    WSR(VBAR_EL1, (uintptr_t)atl_irq_probe_vectors);
    RSR(ICC_SRE_EL1, sre);
    WSR(ICC_SRE_EL1,sre|1);
    RSR(ICC_SRE_EL1,sre);
    if (!(sre&1)) return fail("FAIL:ICC_SRE\n");
    WSR(ICC_IGRPEN1_EL1,0);
    RSR(ICC_CTLR_EL1,mode);
    WSR(ICC_CTLR_EL1,mode&~2);
    RSR(ICC_CTLR_EL1,mode);
    if (mode&2) return fail("FAIL:EOI_MODE\n");
    const uint64_t typer = (uint64_t)rd(GICR(8))|((uint64_t)rd(GICR(12))<<32);
    uint64_t affinity = (capture->mpidr & 0x00ffffff) | ((capture->mpidr>>8)&0xff000000);
    if ((typer>>32)!=affinity) return fail("FAIL:GICR_AFFINITY\n");
    const uint32_t dist = rd(GICD(0));
    if (!(dist & (1u<<6))) return fail("FAIL:GIC_SECURITY_CONFIGURATION\n");
    wr(GICD(0),dist&~7u);
    if (mmio_wait(GICD(0),1u<<31,half_second)) return fail("FAIL:GICD_RWP\n");
    wr(GICD(0),(dist&~7u)|(1u<<4));
    if (mmio_wait(GICD(0),1u<<31,half_second)) return fail("FAIL:GICD_ARE_RWP\n");
    wr(GICR(0x14),rd(GICR(0x14))&~2u);
    if (mmio_wait(GICR(0x14),4,half_second)) return fail("FAIL:GICR_WAKE\n");
    wr(GICR_SGI(0x180),0xffffffffu);
    if (mmio_wait(GICR(0),1u<<3,half_second)) return fail("FAIL:GICR_DISABLE_RWP\n");
    wr(GICR_SGI(0x80),rd(GICR_SGI(0x80))|VIRTUAL_TIMER);
    wr(GICR_SGI(0x280),VIRTUAL_TIMER);
    wr(GICR_SGI(0xc04),rd(GICR_SGI(0xc04))&~(2u<<22));
    *(volatile uint8_t *)GICR_SGI(0x400+VIRTUAL_TIMER_INTID)=0x80;
    wr(GICR_SGI(0x100),VIRTUAL_TIMER);
    if (mmio_wait(GICR(0),1u<<3,half_second)) return fail("FAIL:GICR_ENABLE_RWP\n");
    wr(GICD(0),(dist&~7u)|(1u<<4)|2u);
    if (mmio_wait(GICD(0),1u<<31,half_second)) return fail("FAIL:GICD_ENABLE_RWP\n");
    WSR(ICC_PMR_EL1,0xff);
    WSR(ICC_IGRPEN1_EL1,1);
    barrier();
    loghex("GIC_VERSION=",(rd(GICD(0xffe8))>>4)&15);
    loghex("GICD_CTLR=",rd(GICD(0)));
    loghex("GICR_TYPER=",typer);
    loghex("EOI_MODE=",(mode>>1)&1);
    if (((rd(GICD(0xffe8))>>4)&15)!=3 || !(rd(GICR_SGI(0x80))&VIRTUAL_TIMER) ||
        !(rd(GICR_SGI(0x100))&VIRTUAL_TIMER) || (rd(GICR_SGI(0xc04))&(2u<<22))) return fail("FAIL:GIC_READBACK\n");

    for (uint64_t trial=1; trial<=2; ++trial) {
        irq_period = capture->cntfrq/10;
        uint64_t start = count();
        uint64_t compare = start+capture->cntfrq/(trial==1 ? 100 : 50);
        WSR(CNTV_CVAL_EL0, compare);
        WSR(CNTV_CTL_EL0, 1);
        __asm__ volatile("msr daifclr, #2; isb" : : : "memory");
        while (firings<trial && count()-start<half_second) { }
        if (firings!=trial || failures || acknowledgements!=trial || completions!=trial || irq_now<compare) {
            __asm__ volatile("msr daifset, #2; isb" : : : "memory");
            loghex("ACTUAL_ID=",observed_id);
            loghex("ACTUAL_CTL=",observed_ctl);
            loghex("AFTER_CTL=",after_ctl);
            loghex("AFTER_PENDING=",pending_after);
            return fail("FAIL:TIMER_IRQ_OR_COMPLETION\n");
        }
        /* Leave group1 IRQs unmasked across both the original and revised
           deadlines. IMASK must block a delivery when the revised CVAL expires. */
        uint64_t held_from = irq_now;
        while (firings==trial && count()-held_from<irq_period+capture->cntfrq/100) { }
        __asm__ volatile("msr daifset, #2; isb" : : : "memory");
        if (firings!=trial) return fail("FAIL:MASKED_REDELIVERY\n");
        uint64_t masked_ctl;
        RSR(CNTV_CTL_EL0,masked_ctl);
        if ((masked_ctl&7)!=7 || (rd(GICR_SGI(0x200))&VIRTUAL_TIMER)) return fail("FAIL:MASKED_EXPIRY\n");
        loghex("SHOT=",trial);
        loghex("INTID=",observed_id);
        loghex("ACK_TOTAL=",acknowledgements);
        loghex("EOI_TOTAL=",completions);
        loghex("IRQ_PC=",vector_pc);
        loghex("TIMER_CTL_IRQ=",observed_ctl);
        loghex("TIMER_CTL_DISARMED=",after_ctl);
        loghex("TIMER_CTL_MASKED_EXPIRED=",masked_ctl);
        loghex("GIC_ACTIVE_BEFORE=",active_before);
        loghex("GIC_ACTIVE_AFTER=",active_after);
        loghex("GIC_PENDING_AFTER=",pending_after);
        loghex("LATENESS_TICKS=",irq_now-compare);
        atlantic_early_platform.write("PASS:NO_MASKED_REDELIVERY\n");
        WSR(CNTV_CTL_EL0,0);
    }
    WSR(ICC_IGRPEN1_EL1,0);
    wr(GICR_SGI(0x180),VIRTUAL_TIMER);
    atlantic_early_platform.write("PASS:QEMU_GICV3_VIRTUAL_TIMER_GROUP1\n");
    return 0;
}
