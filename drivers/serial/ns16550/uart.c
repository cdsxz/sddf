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

static bool can_tx_send() 
{
    return (*UART_1_REG(uart_base, LSR) & LSR_THRE);
}

static bool is_tx_full() 
{
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
    while(!can_tx_send());
    if (ch == '\n' ) 
    {
        *UART_1_REG(uart_base, THR) = '\r';
    }
    *UART_1_REG(uart_base, THR) = ch;
}


static bool is_data_ready()
{
    return !(*UART_1_REG(uart_base, DSR) & DSR_RXRDY);
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

static bool is_tx_fifo_full() 
{
    return (*UART_1_REG(uart_base, DSR) & DSR_TXRDY);
}

static void tx_provide(bool is_irq)
{
    bool reprocess = true;
    bool transferred = false;
    while (reprocess) {
        char c;
        while ((!is_irq || !is_tx_fifo_full()) && !serial_dequeue(&tx_queue_handle, &tx_queue_handle.queue->head, &c)) {
            send(c);
            transferred = true;
        }

        serial_request_producer_signal(&tx_queue_handle);
        if (is_tx_fifo_full() && !serial_queue_empty(&tx_queue_handle, tx_queue_handle.queue->head)) {
            enable_tx_interrupt();
        } else {
            disable_tx_interrupt();
        }
        reprocess = false;

        if (!is_tx_fifo_full() && !serial_queue_empty(&tx_queue_handle, tx_queue_handle.queue->head)) {
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

static void rx_return(bool is_irq)
{
    bool reprocess = true;
    bool enqueued = false;
    while (reprocess) {
        while (is_data_ready() && !serial_queue_full(&rx_queue_handle, rx_queue_handle.queue->tail)) {
            char c = read();
            serial_enqueue(&rx_queue_handle, &rx_queue_handle.queue->tail, c);
            enqueued = true;
        }

        if (is_data_ready() && serial_queue_full(&rx_queue_handle, rx_queue_handle.queue->tail)) {
            /* Disable rx interrupts until virtualisers queue is no longer empty. */
            disable_rx_interrupt();
            serial_request_consumer_signal(&rx_queue_handle);
        }
        reprocess = false;

        if (is_data_ready() && !serial_queue_full(&rx_queue_handle, rx_queue_handle.queue->tail)) {
            serial_cancel_consumer_signal(&rx_queue_handle);
            enable_rx_interrupt();
            reprocess = true;
        }
    }

    if (enqueued && serial_require_producer_signal(&rx_queue_handle)) {
        serial_cancel_producer_signal(&rx_queue_handle);
        microkit_notify(RX_CH);
    }
}

static void handle_irq(void)
{
    while (((*UART_1_REG(uart_base, IIR) & IIR_MASK_DA) == IIR_MASK_DA) || 
            ((*UART_1_REG(uart_base, IIR) & IIR_MASK_CTO) == IIR_MASK_CTO) ||
            ((*UART_1_REG(uart_base, IIR) & IIR_MASK_UTHRE) == IIR_MASK_UTHRE)) {
            
        if (((*UART_1_REG(uart_base, IIR) & IIR_MASK_DA) == IIR_MASK_DA) ||
            ((*UART_1_REG(uart_base, IIR) & IIR_MASK_CTO) == IIR_MASK_CTO)) {
            rx_return(true);
        }
        
        if ((*UART_1_REG(uart_base, IIR) & IIR_MASK_UTHRE) == IIR_MASK_UTHRE) {
            tx_provide(true);
        }
    }
}

static void uart_setup(void)
{
    //TODO: initialised from u-boot

    //*UART_1_REG(uart_base, FCR) &= ~FCR_FEN;
    *UART_1_REG(uart_base, FCR) |= FCR_FEN;
    //*UART_1_REG(uart_base, FCR) |= BIT(7) | BIT(6);
    *UART_1_REG(uart_base, FCR) |= FCR_RFR;
    *UART_1_REG(uart_base, FCR) |= FCR_TFR;

    
    enable_rx_interrupt();
    enable_tx_interrupt();
}

void init(void)
{
    uart_setup();

    serial_queue_init(&rx_queue_handle, rx_queue, SERIAL_RX_DATA_REGION_CAPACITY_DRIV, rx_data);

    serial_queue_init(&tx_queue_handle, tx_queue, SERIAL_TX_DATA_REGION_CAPACITY_DRIV, tx_data);
}

void notified(microkit_channel ch)
{
    switch (ch) {
    case IRQ_CH:
        handle_irq();
        microkit_deferred_irq_ack(ch);
        break;
    case TX_CH:
        tx_provide(false);
        break;
    case RX_CH:
        enable_rx_interrupt();
        rx_return(false);
        break;
    default:
        sddf_dprintf("UART|LOG: received notification on unexpected channel: %u\n", ch);
        break;
    }
}
