/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>
#include <sddf/util/printf.h>
#include <serial_config.h>
#include <uart.h>

#define IRQ_CH 0
#define TX_CH  1
#define RX_CH  2

serial_queue_t *rx_queue;
serial_queue_t *tx_queue;

char *rx_data;
char *tx_data;

serial_queue_handle_t rx_queue_handle;
serial_queue_handle_t tx_queue_handle;

uintptr_t uart_base;

/*
    Specific part of the driver
    
*/
static bool can_tx_send() 
{
    /*there is room in the Transmiter Holding Register*/
    return !(*UART_1_REG(uart_base, LSR) & LSR_THRE);
}

static bool is_tx_full() 
{
    /*there is room in the Transmiter Holding Register*/
    if (*UART_1_REG(uart_base, LSR) & LSR_RFE)
    {
        return *UART_1_REG(uart_base, LSR) & LSR_OE;
    }
    return false;
}

static void enable_tx_interrupt()
{
    *UART_1_REG(uart_base, IER) |= IER_ETHREI;
}

static void disable_tx_interrupt()
{
    *UART_1_REG(uart_base, IER) &= ~IER_ETHREI;
}

static void send(char ch)
{  
    if (ch == '\n' ) 
    {
        *UART_1_REG(uart_base, THR) = '\r';
    }
    *UART_1_REG(uart_base, THR) = ch;
}


static bool is_data_ready()
{
    return *UART_1_REG(uart_base, LSR) & LSR_DR;
}

static char read()
{
    return *UART_1_REG(uart_base, RBR) & RBR_MASK;
}

static void enable_rx_interrupt()
{
    *UART_1_REG(uart_base, IER) |= IER_ERDAI;
}

static void disable_rx_interrupt()
{
    *UART_1_REG(uart_base, IER) &= ~IER_ERDAI;
}

/*
    Generic interface : 

*/
static void tx_provide(void)
{
    //microkit_dbg_puts("UART DRIVER|LOG: TX\n");
    bool reprocess = true;
    bool transferred = false;
    while (reprocess) {
        char c;
        while ((*UART_1_REG(uart_base, LSR) & LSR_THRE) && !serial_dequeue(&tx_queue_handle, &tx_queue_handle.queue->head, &c)) {
            send(c);
            transferred = true;
        }

        serial_request_producer_signal(&tx_queue_handle);
        if ( (*UART_1_REG(uart_base, LSR) & LSR_THRE) && !serial_queue_empty(&tx_queue_handle, tx_queue_handle.queue->head)) {
            enable_tx_interrupt();
        } else {
            disable_tx_interrupt();
        }
        reprocess = false;

        if ((*UART_1_REG(uart_base, LSR) & LSR_THRE) && !serial_queue_empty(&tx_queue_handle, tx_queue_handle.queue->head)) {
            serial_cancel_producer_signal(&tx_queue_handle);
            disable_tx_interrupt();
            reprocess = true;
        }
    }

    if (transferred && serial_require_consumer_signal(&tx_queue_handle)) {
        serial_cancel_consumer_signal(&tx_queue_handle);
        microkit_notify(TX_CH);
    }
}

static void rx_return(void)
{
    bool reprocess = true;
    bool enqueued = false;
    while (reprocess) {
        while (is_data_ready() && !serial_queue_full(&rx_queue_handle, rx_queue_handle.queue->tail)) {
            char c = read();
            microkit_dbg_puts("UART DRIVER|OUT: received ");
            microkit_dbg_puts(&c);
            microkit_dbg_puts("\n");
            serial_enqueue(&rx_queue_handle, &rx_queue_handle.queue->tail, c);
            enqueued = true;
        }

        if (is_data_ready() && serial_queue_full(&rx_queue_handle, rx_queue_handle.queue->tail)) {
            //microkit_dbg_puts("UART DRIVER|OUT: received signaled consumer!\n");
            /* Disable rx interrupts until virtualisers queue is no longer empty. */
            disable_rx_interrupt();
            serial_request_consumer_signal(&rx_queue_handle);
        }
        reprocess = false;

        if (is_data_ready() && !serial_queue_full(&rx_queue_handle, rx_queue_handle.queue->tail)) {
            //microkit_dbg_puts("UART DRIVER|OUT: received signal canceled consumer !\n");
            serial_cancel_consumer_signal(&rx_queue_handle);
            enable_rx_interrupt();
            reprocess = true;
        }
    }

    if (enqueued && serial_require_producer_signal(&rx_queue_handle)) {
        //microkit_dbg_puts("UART DRIVER|OUT: received signal canceled producer!\n");
        serial_cancel_producer_signal(&rx_queue_handle);
        microkit_notify(RX_CH);
    }
}


static char hexchar(unsigned int v)
{
    return v < 10 ? '0' + v : ('a' - 10) + v;
}

static void puthex8(uint8_t val)
{
    char buffer[2 + 3];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[2 + 3 - 1] = 0;
    for (unsigned i = 2 + 1; i > 1; i--) {
        buffer[i] = hexchar(val & 0xf);
        val >>= 4;
    }
    microkit_dbg_puts(buffer);
}

static void puthex64(uint64_t val)
{
    char buffer[16 + 3];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[16 + 3 - 1] = 0;
    for (unsigned i = 16 + 1; i > 1; i--) {
        buffer[i] = hexchar(val & 0xf);
        val >>= 4;
    }
    microkit_dbg_puts(buffer);
}

static void handle_irq(void)
{
    microkit_dbg_puts("UART DRIVER|LOG: handle IRQ \n");
    //hexchar(*UART_1_REG(uart_base, IIR));
    //microkit_dbg_puts("\n");
    while (((*UART_1_REG(uart_base, IIR) & IIR_MASK_DA) == IIR_MASK_DA) || 
            ((*UART_1_REG(uart_base, IIR) & IIR_MASK_CTO) == IIR_MASK_CTO) ||
            ((*UART_1_REG(uart_base, IIR) & IIR_MASK_UTHRE) == IIR_MASK_UTHRE)) {
                
            if (((*UART_1_REG(uart_base, IIR) & IIR_MASK_DA) == IIR_MASK_DA) ||
                ((*UART_1_REG(uart_base, IIR) & IIR_MASK_CTO) == IIR_MASK_CTO)) {
                rx_return();
            }
            
            if ((*UART_1_REG(uart_base, IIR) & IIR_MASK_UTHRE) == IIR_MASK_UTHRE) {
                tx_provide();
            }
    }
}

static void uart_setup(void)
{
    //TODO: initialised from u-boot
    /*
    while (!(*UART_1_REG(uart_base, LSR) & LSR_TEMT));
    
    *UART_1_REG(uart_base, IER) = 0x00;
    *UART_1_REG(uart_base, MCR) = 0x02;
    *UART_1_REG(uart_base, FCR) = BIT(0);
    *UART_1_REG(uart_base, LCR) = 0x03;
    */
    *UART_1_REG(uart_base, IER) |= IER_ERDAI;
   //disable_tx_interrupt();
}

void init(void)
{
    //microkit_dbg_puts("UART DRIVER|LOG: start\n");
    uart_setup();

    serial_queue_init(&rx_queue_handle, rx_queue, SERIAL_RX_DATA_REGION_CAPACITY_DRIV, rx_data);

    serial_queue_init(&tx_queue_handle, tx_queue, SERIAL_TX_DATA_REGION_CAPACITY_DRIV, tx_data);
    //microkit_dbg_puts("UART DRIVER|LOG: end\n");
}

void notified(microkit_channel ch)
{
    switch (ch) {
    case IRQ_CH:
        send('I');
        handle_irq();
        microkit_deferred_irq_ack(ch);
        break;
    case TX_CH:
        send('T');
        tx_provide();
        break;
    case RX_CH:
        send('R');
        enable_rx_interrupt();
        //uart_regs->imsc |= (PL011_IMSC_RX_TIMEOUT | PL011_IMSC_RX_INT);

        rx_return();
        /*
        if (is_data_ready() ) {
            send('1');
            char c = read();
            send('2');
            while(!(*UART_1_REG(uart_base, LSR) & LSR_THRE));
            send(c);
        }
        */
        break;
    default:
        sddf_dprintf("UART|LOG: received notification on unexpected channel: %u\n", ch);
        break;
    }
}
