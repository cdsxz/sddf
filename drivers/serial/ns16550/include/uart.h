/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <sddf/util/util.h>
#include <sddf/serial/queue.h>

/*
 * This UART driver is based on the following specificion:
 * QorIQ LS1043A Reference Manual
 * Revision: 6, 07/2020
 */

#define OFFSET_DUART_1 0x500
#define OFFSET_DUART_2 0x600

/* Register by offset */
#define THR        0x0 /* receiver buffer register */
#define RBR        0x0 /* transmitter holding register */
#define RBR_MASK   0x000000ff

#define IER        0x1 /* interrupt enable register */
#define IER_EMSI   BIT(3) /* Enable modem status interrupt */
#define IER_ERLSI  BIT(2) /* Enable receiver line status interrupt */
#define IER_ETHREI BIT(1) /* Enable transmitter holding register empty interrupt */
#define IER_ERDAI  BIT(0) /* Enable received data available interrupt */

#define FCR        0x2 /* FIFO control register */
#define IIR        0x2 /* interrupt ID register */
#define IIR_MASK_RLS    0b00000110 /* receiver line status */
#define IIR_MASK_DA     0b00000100 /* data available */
#define IIR_MASK_CTO    0b00001100 /* character timeout */
#define IIR_MASK_UTHRE  0b00000010 /* UTHR empty */
#define IIR_MASK_MS     0b00000000 /* Modem status */

#define LCR        0x3 /* line control register */

#define MCR        0x4 /* modem control register */

#define LSR        0x5 /* line status register */
#define LSR_RFE    BIT(7) /* Receiver FIFO error */
#define LSR_TEMT   BIT(6) /* Transmitter empty */
#define LSR_THRE   BIT(5) /* Transmitter holding register empty */
#define LSR_BI     BIT(4) /* Break interrupt */
#define LSR_FE     BIT(3) /* Framing error */
#define LSR_PE     BIT(2) /* Parity error */
#define LSR_OE     BIT(1) /* Overrun error */
#define LSR_DR     BIT(0) /* Data ready */

#define MSR        0x6 /* modem status register */

#define UART_1_REG(mmio, x) ((volatile uint8_t *)((mmio) + OFFSET_DUART_1 + (x)))
#define UART_2_REG(mmio, x) ((volatile uint8_t *)((mmio) + OFFSET_DUART_2 + (x)))
