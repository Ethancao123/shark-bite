#include <avr/io.h>
#include <avr/interrupt.h>

#define F_CPU 10000000 /* 10MHz / prescale=2 */
#include <util/delay.h>

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>

#include "diag.h"
#include "a7105_spi.h"
#include "radio.h"
#include "state.h"
#include "motors.h"
#include "nvconfig.h"
#include "mixing.h"

volatile master_state_t master_state;
extern const char * end_marker;

static void init_clock()
{
    // This is where we change the cpu clock if required.
    uint8_t val = CLKCTRL_PDIV_2X_gc | 0x1;  // 0x1 = PEN enable prescaler.
    _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, val);
    _delay_ms(10);
}

static void init_serial()
{
    // UART0- need to use "alternate" pins 
    // This puts TxD and RxD on PA1 and PA2
    PORTMUX.CTRLB = PORTMUX_USART0_ALTERNATE_gc; 
    // Diagnostic uart output       
    // TxD pin PA1 is used for diag, should be an output
    // And set it initially high
    PORTA.OUTSET = 1 << 1;
    PORTA.DIRSET = 1 << 1;
    
    uint32_t want_baud_hz = 230400; // Baud rate (was 115200)
    uint32_t clk_per_hz = F_CPU; // CLK_PER after prescaler in hz
    uint16_t baud_param = (64 * clk_per_hz) / (16 * want_baud_hz);
    USART0.BAUD = baud_param;
    USART0.CTRLB = 
        USART_TXEN_bm | USART_RXEN_bm; // Start Transmitter and receiver
    // Enable interrupts from the usart rx
    // USART0.CTRLA |= USART_RXCIE_bm;
}

static void uninit_serial()
{
    USART0.CTRLB = 0; //disable rx and tx
    PORTA.DIRCLR = 1 << 1; // set as input.
}

static void init_timer()
{
    // This uses TCB1 for timer interrupts.
    // We need to count 100k clock cycles, so count to 50k
    // and enable divide by 2
    const unsigned short compare_value = 50000; 
    TCB1.CCMP = compare_value;
    TCB1.INTCTRL = TCB_CAPT_bm;
    // CTRLB bits 0-2 are the mode, which by default
    // 000 is "periodic interrupt" -which is correct
    TCB1.CTRLB = 0;
    TCB1.CNT = 0;
    // CTRLA- select CLK_PER/2 and enable.
    TCB1.CTRLA = TCB_ENABLE_bm | TCB_CLKSEL_CLKDIV2_gc;
    master_state.tickcount = 0;
}

ISR(TCB1_INT_vect)
{
    master_state.tickcount++;
    TCB1.INTFLAGS |= TCB_CAPT_bm; //clear the interrupt flag
}

void trigger_reset()
{
    // Pull the reset line.
    cli();
    while (1) {
        _PROTECTED_WRITE(RSTCTRL.SWRR, 1);
    }
}

uint32_t get_tickcount()
{
    uint32_t tc;
    cli(); // disable irq
    tc = master_state.tickcount;
    sei(); // enable irq
    return tc;
}

void epic_fail(const char * reason)
{
    diag_puts(reason);
    diag_puts("\r\nFAIL FAIL FAIL!\r\n\n\n");
    uninit_serial();
    _delay_ms(250);
    trigger_reset(); // Reset the whole device to try again.
}

static void integrity_check()
{
    /*
     * Check some data at the end of the flash image, to see if
     * they are present and correct. This probably means that
     * the flash was successful.
     */
    // Check end_marker
    int l = strlen(end_marker);
    if ((l != 8) || (end_marker[0] != 'S')) {
        epic_fail("Integrity check failed");
    }
    diag_println("Integrity check ok");
}

int main(void)
{
    init_clock();
    init_serial();
    // Write the greeting message as soon as possible.
    diag_println("\r\nMalenki-nano2 receiver starting up");
    integrity_check();
    init_timer();
    sei(); // interrupts on
    // test_get_micros();
    spi_init();
    motors_init();
    mixing_init();
    nvconfig_load();
    radio_init();
    
    while(1) {
        // Main loop
        radio_loop();
    }
}
