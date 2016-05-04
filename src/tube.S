#include "rpi-base.h"
#include "tube-defs.h"

.text
.global arm_irq_handler
.global arm_firq_handler
.global copro_65tube_tube_read
.global copro_65tube_tube_write

.macro ASSERT_LOW mask num
        ldr     r0, =GPLEV0
        ldr     r1, [r0]        
        tst     r1, #\mask
        mov     r0, #\num
        blne    assert_fail    
.endm

.macro ASSERT_HIGH mask num
        ldr     r0, =GPLEV0
        ldr     r1, [r0]        
        tst     r1, #\mask
        mov     r0, #\num
        bleq    assert_fail    
.endm
        
        
// ARM IRQ handler

// This is the original IRQ handler from PiTubeClient's copro-65tube
//
// Retained for reference so we can see how events (IRQ, NMI, RST)
// are signalled to the 6502 code.
//
// If we keep this same "interface" then we should be able to re-use that code without change
//
//arm_irq_handler:
//        push    {r0, r1, r2}
//        ldr     r0, =(PERIPHERAL_BASE + 0x200040) // any falling events?
//        ldr     r2, [r0]
//        mov     r1, #1<<7               // mask our events
//        orr     r1, r1, #1<<18 + 1<<17
//        and     r2, r2, r1
//        str     r2, [r0]                // clear Pin events
//        ldr     r1, events
//        orr     r1, r1, r2              // add our events in
//        str     r1, events
//        orr     ip, ip, #256<<I_ALIGN   // signal event to 6502 instruction flow
//        pop     {r0, r1, r2}
//        subs    pc, lr, #4


arm_firq_handler:
        subs    pc, lr, #4
        
arm_irq_handler:
        
        push    {r0-r3,ip,lr}
        
// toggle a telltale pin (set)
//        ldr     r0, =GPSET0
//        mov     r1, #NRST_MASK
//        str     r1, [r0]

        ldr     r0, =GPEDS0
        mov     r1, #-1
        str     r1, [r0]                // clear all Pin events

        ldr     r0, =GPLEV0
        ldr     r1, [r0]                // *** very expensive but necessary ***

        tst     r1, #NTUBE_MASK         // test for glitches
        bne     exit

        tst     r1, #RNW_MASK
        beq     wr_cycle

// READ_CYCLE
        mov     r2, #0
        tst     r1, #A0_MASK
        orrne   r2, #1
        tst     r1, #A1_MASK
        orrne   r2, #2
        tst     r1, #A2_MASK
        orrne   r2, #4

        ldr     r0, =tube_regs
        ldrb    r2, [r0, r2]

        mov     r3, r2, LSL #D0_BASE // low nibble now aligned to GPIO
        and     r3, r3, #D30_MASK
        mov     r2, r2, LSL #D4_BASE-4 
        and     r2, r2, #D74_MASK // high nibble in the right place
        orr     r2, r2, r3
        
        ldr     r0, =GPSET0
        str     r2, [r0]                // *** expensive but necessary ***
        eor     r2, r2, #D30_MASK
        eor     r2, r2, #D74_MASK
        ldr     r0, =GPCLR0
        str     r2, [r0]                // *** expensive but necessary ***
        
        
        ldr     r0, =GPFSEL0
        ldr     r3, =(gpfsel_data_driving)
        ldr     r1, [r3]
        str     r1, [r0]                // *** expensive but necessary ***
        ldr     r1, [r3, #4]
        str     r1, [r0, #4]            // *** expensive but necessary ***
        ldr     r1, [r3, #8]
        str     r1, [r0, #8]            // *** expensive but necessary ***

// toggle a telltale pin (clear)
//        ldr     r0, =GPCLR0
//        mov     r1, #NRST_MASK
//        str     r1, [r0]
        
        ldr     r0, =GPLEV0
rd_wait_for_phi2_low:
        ldr     r1, [r0]                // ** very expensive but necessary ***     
        tst     r1, #PHI2_MASK
        bne     rd_wait_for_phi2_low

        ldr     r0, =GPFSEL0
        ldr     r3, =(gpfsel_data_idle)
        ldr     r1, [r3]
        str     r1, [r0]                // *** expensive but necessary ***
        ldr     r1, [r3, #4]
        str     r1, [r0, #4]            // *** expensive but necessary ***
        ldr     r1, [r3, #8]
        str     r1, [r0, #8]            // *** expensive but necessary ***
        
//        ldr     r0, =GPLEV0
//        ldr     r1, [r0]        
//        bl      temp_tube_io_handler
        
        b       exit
        
// WRITE_CYCLE
wr_cycle:
         
wr_wait_for_phi2_high:
        ldr     r1, [r0]        
        tst     r1, #PHI2_MASK
        beq     wr_wait_for_phi2_high

        
wr_wait_for_phi2_low:
        ldr     r1, [r0]        
        tst     r1, #PHI2_MASK
        bne     wr_wait_for_phi2_low
                
//        bl      temp_tube_io_handler

exit:
        pop     {r0-r3,ip,lr}
        subs    pc, lr, #4


// 8 BYTES OF STATE
tube_regs:
        .byte 0xfe
        .byte 0xed
        .byte 0xdc
        .byte 0xcb
        .byte 0xba
        .byte 0xa9
        .byte 0x98
        .byte 0x87
        
// parasite reading the tube
copro_65tube_tube_read:
    mov      pc, lr

// parasite writing the tube
copro_65tube_tube_write:
    mov      pc, lr
        