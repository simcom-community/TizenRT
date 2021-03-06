/****************************************************************************
 *
 * Copyright 2020 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/
/****************************************************************************
 *
 *   Copyright (C) 2020 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <semaphore.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <tinyara/irq.h>
#include <tinyara/arch.h>
#include <tinyara/serial/serial.h>
#include <tinyara/pm/pm.h>

#ifdef CONFIG_SERIAL_TERMIOS
#include <termios.h>
#endif

#include <arch/serial.h>
#include <arch/board/board.h>

#include "chip.h"
#include "up_arch.h"
#include "up_internal.h"

#include "mbed/hal/serial_api.h"
#include "mbed/targets/hal/rtl8721d/PinNames.h"
#include "mbed/targets/hal/rtl8721d/objects.h"
#include "rtl8721d_uart.h"
#include "tinyara/kmalloc.h"
/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/
/*
 * If we are not using the serial driver for the console, then we
 * still must provide some minimal implementation of up_putc.
 */

#undef TTYS0_DEV
#undef TTYS1_DEV
#undef TTYS2_DEV

#undef UART0_ASSIGNED
#undef UART1_ASSIGNED
#undef UART2_ASSIGNED

/* Which UART with be ttyS0/console and which ttyS1? ttyS2 */
/* First pick the console and ttys0. This could be any of UART0-2 */
#if defined(CONFIG_UART0_SERIAL_CONSOLE)
#define CONSOLE_DEV             g_uart0port             /* UART0 is console */
#define TTYS0_DEV               g_uart0port             /* UART0 is ttyS0 */
#define UART0_ASSIGNED  1
#define HAVE_SERIAL_CONSOLE
#elif defined(CONFIG_UART1_SERIAL_CONSOLE)
#define CONSOLE_DEV             g_uart1port             /* UART1 is console */
#define TTYS0_DEV               g_uart1port             /* UART1 is ttyS0 */
#define UART1_ASSIGNED  1
#define HAVE_SERIAL_CONSOLE
#elif defined(CONFIG_UART2_SERIAL_CONSOLE)
#define CONSOLE_DEV             g_uart2port             /* UART2 is console */
#define TTYS0_DEV               g_uart2port             /* UART2 is ttyS0 */
#define UART2_ASSIGNED  1
#define HAVE_SERIAL_CONSOLE
#else
#undef CONSOLE_DEV                                              /* No console */
#if defined(CONFIG_RTL8721D_UART0)
#define TTYS0_DEV               g_uart0port             /* UART0 is ttyS0 */
#define UART0_ASSIGNED  1
#elif defined(CONFIG_RTL8721D_UART1)
#define TTYS0_DEV               g_uart1port             /* UART1 is ttyS0 */
#define UART1_ASSIGNED  1
#elif defined(CONFIG_RTL8721D_UART2)
#define TTYS0_DEV               g_uart2port             /* UART2 is ttyS0 */
#define UART2_ASSIGNED  1
#endif
#endif

/* Pick ttyS1. This could be any of UART0-2 excluding the console UART. */
#if defined(CONFIG_RTL8721D_UART0) && !defined(UART0_ASSIGNED)
#define TTYS1_DEV               g_uart0port             /* UART0 is ttyS1 */
#define UART0_ASSIGNED  1
#elif defined(CONFIG_RTL8721D_UART1) && !defined(UART1_ASSIGNED)
#define TTYS1_DEV               g_uart1port             /* UART1 is ttyS1 */
#define UART1_ASSIGNED  1
#elif defined(CONFIG_RTL8721D_UART2) && !defined(UART2_ASSIGNED)
#define TTYS1_DEV               g_uart2port             /* UART2 is ttyS1 */
#define UART2_ASSIGNED  1
#endif

/*
 * Pick ttyS2. This could be one of UART1-2. It can't be UART0 because that
 * was either assigned as ttyS0 or ttyS1. One of these could also be the
 * console
 */
#if defined(CONFIG_RTL8721D_UART1) && !defined(UART1_ASSIGNED)
#define TTYS2_DEV               g_uart1port             /* UART1 is ttyS2 */
#define UART1_ASSIGNED  1
#elif defined(CONFIG_RTL8721D_UART2) && !defined(UART2_ASSIGNED)
#define TTYS2_DEV               g_uart2port             /* UART2 is ttyS2 */
#define UART2_ASSIGNED  1
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/
/*
 * Available UARTs in AmebaD
 * UART0_DEV: KM4 uart0
 * UART1_DEV: KM4 uart1_bt
 * UART2_DEV: KM0 log uart
 * UART3_DEV: KM0 luart
 */
static serial_t* sdrv[MAX_UART_INDEX + 1] = {NULL, NULL, NULL, NULL, NULL}; //uart 0~3, uart2 is configured as log uart

/* UART configuration */
static UART_InitTypeDef UART_INIT = {
				.Parity			= RUART_PARITY_ENABLE,
				.ParityType		= RUART_ODD_PARITY,
				.StickParity		= RUART_STICK_PARITY_DISABLE,
				.StopBit		= RUART_STOP_BIT_1,
				.WordLen		= RUART_WLS_8BITS,
				.RxFifoTrigLevel	= 1,
				.DmaModeCtrl		= 1,
				.FlowControl		= 0,
				.RxTimeOutCnt		= 64,
};

struct rtl8721d_up_dev_s {
	uint32_t DmaModeCtrl;
	uint32_t WordLen;
        uint32_t StopBit;                       /* true: Configure with 2 stop bits instead of 1 */
        uint32_t Parity;                        /* 0=disable, 1=enable */
        uint32_t ParityType;                    /* 0=none, 1=odd, 2=even */
        uint32_t StickParity;
	uint32_t FlowControl;
	uint32_t RxFifoTrigLevel;
	uint32_t RxErReportCtrl;
	uint32_t RxTimeOutCnt;
	uint32_t baud;                          /* Configured baud rate */
	uint32_t irq;                           /* IRQ associated with this UART */
	PinName Tx;				/* TX UART pin number */
	PinName Rx;				/* RX UART pin number */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int rtl8721d_up_setup(struct uart_dev_s *dev);
static void rtl8721d_up_shutdown(struct uart_dev_s *dev);
static int rtl8721d_up_attach(struct uart_dev_s *dev);
static void rtl8721d_up_detach(struct uart_dev_s *dev);
static int rtl8721d_up_interrupt(int irq, void *context, FAR void *arg);
static int rtl8721d_up_ioctl(struct file *filep, int cmd, unsigned long arg);
static int rtl8721d_up_receive(struct uart_dev_s *dev, uint8_t *status);
static void rtl8721d_up_rxint(struct uart_dev_s *dev, bool enable);
static bool rtl8721d_up_rxavailable(struct uart_dev_s *dev);
static void rtl8721d_up_send(struct uart_dev_s *dev, int ch);
static void rtl8721d_up_txint(struct uart_dev_s *dev, bool enable);
static bool rtl8721d_up_txready(struct uart_dev_s *dev);
static bool rtl8721d_up_txempty(struct uart_dev_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Serial driver UART operations */

static const struct uart_ops_s g_uart_ops = {
        .setup = rtl8721d_up_setup,
        .shutdown = rtl8721d_up_shutdown,
        .attach = rtl8721d_up_attach,
        .detach = rtl8721d_up_detach,
        .ioctl = rtl8721d_up_ioctl,
        .receive = rtl8721d_up_receive,
        .rxint = rtl8721d_up_rxint,
        .rxavailable = rtl8721d_up_rxavailable,
#ifdef CONFIG_SERIAL_IFLOWCONTROL
        .rxflowcontrol = NULL,
#endif
        .send = rtl8721d_up_send,
        .txint = rtl8721d_up_txint,
        .txready = rtl8721d_up_txready,
        .txempty = rtl8721d_up_txempty,
};

#ifdef CONFIG_RTL8721D_UART0
static char g_uart0rxbuffer[CONFIG_UART0_RXBUFSIZE];
static char g_uart0txbuffer[CONFIG_UART0_TXBUFSIZE];
#endif
#ifdef CONFIG_RTL8721D_UART1
static char g_uart1rxbuffer[CONFIG_UART1_RXBUFSIZE];
static char g_uart1txbuffer[CONFIG_UART1_TXBUFSIZE];
#endif
#ifdef CONFIG_RTL8721D_UART2
static char g_uart2rxbuffer[CONFIG_UART2_RXBUFSIZE];
static char g_uart2txbuffer[CONFIG_UART2_TXBUFSIZE];
#endif

#define RTL8721D_UART0_IRQ	(50)
#define RTL8721D_UART1_IRQ	(51)
#define RTL8721D_UART_LOG_IRQ	(19)

#ifdef CONFIG_RTL8721D_UART0
static struct rtl8721d_up_dev_s g_uart0priv = {
	.Parity = CONFIG_UART0_PARITY,
	.ParityType = RUART_ODD_PARITY,
	.StickParity = RUART_STICK_PARITY_DISABLE,
	.StopBit = CONFIG_UART0_2STOP,
	.WordLen = CONFIG_UART0_BITS,
	.FlowControl = FlowControlNone,
	.irq = RTL8721D_UART0_IRQ,
	.baud = CONFIG_UART0_BAUD,
	.Tx = PA_21,
	.Rx = PA_22,
};

static uart_dev_t g_uart0port = {
	.isconsole = false,
	.recv = {
		.size = CONFIG_UART0_RXBUFSIZE,
		.buffer = g_uart0rxbuffer,
	},
	.xmit = {
		.size = CONFIG_UART0_TXBUFSIZE,
		.buffer = g_uart0txbuffer,
	},
	.ops = &g_uart_ops,
	.priv = &g_uart0priv,
};
#endif

#ifdef CONFIG_RTL8721D_UART1
static struct rtl8721d_up_dev_s g_uart1priv = {
	.Parity = CONFIG_UART1_PARITY,
	.ParityType = RUART_ODD_PARITY,
	.StickParity = RUART_STICK_PARITY_DISABLE,
	.StopBit = CONFIG_UART1_2STOP,
	.WordLen = CONFIG_UART1_BITS,
	.FlowControl = FlowControlNone,
	.irq = RTL8721D_UART1_IRQ,
	.baud = CONFIG_UART1_BAUD,
	.Tx = PA_12,
	.Rx = PA_13,
};

static uart_dev_t g_uart1port = {
	.isconsole = false,
	.recv = {
		.size = CONFIG_UART1_RXBUFSIZE,
		.buffer = g_uart1rxbuffer,
	},
	.xmit = {
		.size = CONFIG_UART1_TXBUFSIZE,
		.buffer = g_uart1txbuffer,
	},
	.ops = &g_uart_ops,
	.priv = &g_uart1priv,
};
#endif

#ifdef CONFIG_RTL8721D_UART2
static struct rtl8721d_up_dev_s g_uart2priv = {
	.Parity = CONFIG_UART2_PARITY,
	.ParityType = RUART_ODD_PARITY,
	.StickParity = RUART_STICK_PARITY_DISABLE,
	.StopBit = CONFIG_UART2_2STOP,
	.FlowControl = FlowControlNone,
	.WordLen = CONFIG_UART2_BITS,
	.irq = RTL8721D_UART_LOG_IRQ,
	.baud = CONFIG_UART2_BAUD,
	.Tx = PA_7,
	.Rx = PA_8,
};

static uart_dev_t g_uart2port = {
	.isconsole = true,
	.recv = {
		.size = CONFIG_UART2_RXBUFSIZE,
		.buffer = g_uart2rxbuffer,
	},
	.xmit = {
		.size = CONFIG_UART1_TXBUFSIZE,
		.buffer = g_uart2txbuffer,
	},
	.ops = &g_uart_ops,
	.priv = &g_uart2priv,
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static u32 uart_index_get(PinName tx)
{
	if ((tx == _PA_12) ||(tx == _PB_1)|| (tx == _PA_26)) {
		return 3;
	} else if ((tx == _PA_18) || (tx == _PA_21)||(tx == _PB_9)||(tx == _PB_19)) {
		return 0;
	}else if (tx == _PA_7){
		return 2;
	} else {
		assert_param(0);
	}
        return 3;
}

/****************************************************************************
 * Name: rtl8721d_up_setup
 *
 * Description:
 *   Configure the UART baud, bits, parity, fifos, etc. This
 *   method is called the first time that the serial port is
 *   opened.
 *
 ****************************************************************************/
static int rtl8721d_up_setup(struct uart_dev_s *dev)
{
	struct rtl8721d_up_dev_s *priv = (struct rtl8721d_up_dev_s *)dev->priv;
	DEBUGASSERT(priv);
	DEBUGASSERT(!sdrv[uart_index_get(priv->Tx)]);

	sdrv[uart_index_get(priv->Tx)] = (serial_t *)kmm_malloc(sizeof(serial_t));
	DEBUGASSERT(sdrv[uart_index_get(priv->Tx)]);
	serial_init((serial_t*)sdrv[uart_index_get(priv->Tx)], priv->Tx, priv->Rx);
	serial_baud(sdrv[uart_index_get(priv->Tx)], priv->baud);
	serial_format(sdrv[uart_index_get(priv->Tx)], priv->WordLen, priv->Parity, priv->StopBit);
	serial_set_flow_control(sdrv[uart_index_get(priv->Tx)], priv->FlowControl, priv->Tx, priv->Rx);
	serial_enable(sdrv[uart_index_get(priv->Tx)]);

	return OK;
}

/****************************************************************************
 * Name: up_shutdown
 *
 * Description:
 *   Disable the UART.  This method is called when the serial
 *   port is closed
 *
 ****************************************************************************/

static void rtl8721d_up_shutdown(struct uart_dev_s *dev)
{
	struct rtl8721d_up_dev_s *priv = (struct rtl8721d_up_dev_s *)dev->priv;
	DEBUGASSERT(priv);
	DEBUGASSERT(sdrv[uart_index_get(priv->Tx)]);
	serial_free(sdrv[uart_index_get(priv->Tx)]);
	free(sdrv[uart_index_get(priv->Tx)]);
	sdrv[uart_index_get(priv->Tx)] = NULL;
}

/****************************************************************************
 * Name: up_attach
 *
 * Description:
 *   Configure the UART to operation in interrupt driven mode.  This method is
 *   called when the serial port is opened.  Normally, this is just after the
 *   the setup() method is called, however, the serial console may operate in
 *   a non-interrupt driven mode during the boot phase.
 *
 *   RX and TX interrupts are not enabled when by the attach method (unless the
 *   hardware supports multiple levels of interrupt enabling).  The RX and TX
 *   interrupts are not enabled until the txint() and rxint() methods are called.
 *
 ****************************************************************************/

//extern uint32_t uart_irqhandler(void *data);
void rtl8721d_uart_irq(uint32_t id, SerialIrq event)
{
	struct uart_dev_s *dev = (struct uart_dev_s *)id;
	struct rtl8721d_up_dev_s *priv = (struct rtl8721d_up_dev_s *)dev->priv;
	if(event == RxIrq) {
		uart_recvchars(dev);
	}
	if(event == TxIrq){
		uart_xmitchars(dev);
	}
}
static int rtl8721d_up_attach(struct uart_dev_s *dev)
{
	struct rtl8721d_up_dev_s *priv = (struct rtl8721d_up_dev_s *)dev->priv;
	int ret = 0;
	DEBUGASSERT(priv);
#if 0
	/* Attach and enable the IRQ */
	ret = irq_attach(priv->irq, rtl8721d_up_interrupt, NULL);		//UART_LOG_IRQ(19)
	if (ret == OK) {
		/* Enable the interrupt (RX and TX interrupts are still disabled
		 * in the UART
		 */

		up_enable_irq(priv->irq);
	}
#else
	serial_irq_handler(sdrv[uart_index_get(priv->Tx)], rtl8721d_uart_irq, (uint32_t)dev);
#endif
	return ret;
}

/****************************************************************************
 * Name: up_detach
 *
 * Description:
 *   Detach UART interrupts.  This method is called when the serial port is
 *   closed normally just before the shutdown method is called.  The exception is
 *   the serial console which is never shutdown.
 *
 ****************************************************************************/

static void rtl8721d_up_detach(struct uart_dev_s *dev)
{
	struct rtl8721d_up_dev_s *priv = (struct rtl8721d_up_dev_s *)dev->priv;
	DEBUGASSERT(priv);
#if 0
	up_disable_irq(priv->irq);
	irq_detach(priv->irq);
#else
	serial_irq_handler(sdrv[uart_index_get(priv->Tx)], NULL, 0);
#endif
}

/****************************************************************************
 * Name: rtl8721d_up_interrupt
 *
 * Description:
 *   This is the UART interrupt handler.  It will be invoked
 *   when an interrupt received on the 'irq'  It should call
 *   uart_transmitchars or uart_receivechar to perform the
 *   appropriate data transfers.  The interrupt handling logic\
 *   must be able to map the 'irq' number into the approprite
 *   uart_dev_s structure in order to call these functions.
 *
 ****************************************************************************/
#if 0
static int rtl8721d_up_interrupt(int irq, void *context, FAR void *arg)
{
        struct uart_dev_s *dev = NULL;
        uint8_t IntId;

        switch (irq) {
#ifdef CONFIG_RTL8721D_UART0
        case RTL8721D_UART0_IRQ:
		dev = &g_uart0port;
		break;
#endif
#ifdef CONFIG_RTL8721D_UART1
        case RTL8721D_UART1_IRQ:
		dev = &g_uart1port;
		break;
#endif
#ifdef CONFIG_RTL8721D_UART2
        case RTL8721D_UART_LOG_IRQ:
		dev = &g_uart2port;
		break;
#endif
        default:
		PANIC();
		break;
        }

	IntId = uart_get_interrupt_id(sdrv);

        switch (IntId) {
        case RUART_LP_RX_MONITOR_DONE:
		uart_irqhandler(IntId);
        break;

        case RUART_MODEM_STATUS:
		uart_irqhandler(IntId);
        break;

        case RUART_RECEIVE_LINE_STATUS:
		uart_irqhandler(IntId);
        break;

        case RUART_TX_FIFO_EMPTY:
		uart_xmitchars(dev);
        break;

        case RUART_RECEIVER_DATA_AVAILABLE:
        case RUART_TIME_OUT_INDICATION:
		uart_recvchars(dev);
        break;

        default:
		DEBUGASSERT("Unknown Interrupt !!\n");
        break;
        }

        return OK;
}
#endif
/****************************************************************************
 * Name: up_ioctl
 *
 * Description:
 *   All ioctl calls will be routed through this method
 *
 ****************************************************************************/

static int rtl8721d_up_ioctl(struct file *filep, int cmd, unsigned long arg)
{
	struct inode *inode = filep->f_inode;
	struct uart_dev_s *dev = inode->i_private;
	struct rtl8721d_up_dev_s *priv = (struct rtl8721d_up_dev_s *)dev->priv;
	int ret = OK;
	struct termios *termiosp = (struct termios *)arg;

	DEBUGASSERT(priv);
	switch (cmd) {
	case TCGETS:
		if (!termiosp) {
			return -EINVAL;
		}

		cfsetispeed(termiosp, priv->baud);

		termiosp->c_cflag = 0;

		if (priv->Parity) {
			termiosp->c_cflag |= PARENB;
			if (priv->Parity == RUART_ODD_PARITY) {
				termiosp->c_cflag |= PARODD;
			}
		}

		if (priv->StopBit) {
			termiosp->c_cflag |= CSTOPB;
		}

		termiosp->c_cflag |= CS5 + (8 - 5);
		break;

	case TCSETS:
		if (!termiosp) {
			return -EINVAL;
		}

		priv->WordLen = 5 + (termiosp->c_cflag & CSIZE);

		priv->Parity = 0;
		if (termiosp->c_cflag & PARENB) {
			if (termiosp->c_cflag & PARODD) {
				priv->Parity = RUART_ODD_PARITY;
			} else {
				priv->Parity = RUART_EVEN_PARITY;
			}
		}
		priv->StopBit = !!(termiosp->c_cflag & CSTOPB);

		priv->baud = cfgetispeed(termiosp);
		if(sdrv[uart_index_get(priv->Tx)])
			rtl8721d_up_shutdown(dev);
		rtl8721d_up_setup(dev);
		break;

	default:
		ret = -ENOTTY;
		break;
	}
	return ret;
}

/****************************************************************************
 * Name: up_receive
 *
 * Description:
 *   Called (usually) from the interrupt level to receive one
 *   character from the UART.  Error bits associated with the
 *   receipt are provided in the return 'status'.
 *
 ****************************************************************************/

static int rtl8721d_up_receive(struct uart_dev_s *dev, uint8_t *status)
{
	struct rtl8721d_up_dev_s *priv = (struct rtl8721d_up_dev_s *)dev->priv;
	uint32_t rxd;

	DEBUGASSERT(priv);
	rxd = serial_getc(sdrv[uart_index_get(priv->Tx)]);
	*status = rxd;

	return rxd & 0xff;
}

/****************************************************************************
 * Name: up_rxint
 *
 * Description:
 *   Call to enable or disable RX interrupts
 *
 ****************************************************************************/
static void rtl8721d_up_rxint(struct uart_dev_s *dev, bool enable)
{
	struct rtl8721d_up_dev_s *priv = (struct rtl8721d_up_dev_s *)dev->priv;
	DEBUGASSERT(priv);
	serial_irq_set(sdrv[uart_index_get(priv->Tx)], RxIrq, enable); // 1= ENABLE
}

/****************************************************************************
 * Name: up_rxavailable
 *
 * Description:
 *   Return true if the receive fifo is not empty
 *
 ****************************************************************************/

static bool rtl8721d_up_rxavailable(struct uart_dev_s *dev)
{
	struct rtl8721d_up_dev_s *priv = (struct rtl8721d_up_dev_s *)dev->priv;
	DEBUGASSERT(priv);
	return(serial_readable(sdrv[uart_index_get(priv->Tx)]));
}

/****************************************************************************
 * Name: up_send
 *
 * Description:
 *   This method will send one byte on the UART
 *
 ****************************************************************************/

static void rtl8721d_up_send(struct uart_dev_s *dev, int ch)
{
	struct rtl8721d_up_dev_s *priv = (struct rtl8721d_up_dev_s *)dev->priv;
	DEBUGASSERT(priv);
	/*write one byte to tx fifo*/
	serial_putc(sdrv[uart_index_get(priv->Tx)], ch);
}

/****************************************************************************
 * Name: up_txint
 *
 * Description:
 *   Call to enable or disable TX interrupts
 *
 ****************************************************************************/

static void rtl8721d_up_txint(struct uart_dev_s *dev, bool enable)
{
	struct rtl8721d_up_dev_s *priv = (struct rtl8721d_up_dev_s *)dev->priv;
	DEBUGASSERT(priv);
	serial_irq_set(sdrv[uart_index_get(priv->Tx)], TxIrq, enable);
	if(enable)
		UART_INTConfig(UART_DEV_TABLE[uart_index_get(priv->Tx)].UARTx, RUART_IER_ETBEI, ENABLE);
}

/****************************************************************************
 * Name: up_txready
 *
 * Description:
 *   Return true if the tranmsit fifo is not full
 *
 ****************************************************************************/

static bool rtl8721d_up_txready(struct uart_dev_s *dev)
{
	struct rtl8721d_up_dev_s *priv = (struct rtl8721d_up_dev_s *)dev->priv;
	DEBUGASSERT(priv);
	while (!serial_writable(sdrv[uart_index_get(priv->Tx)]));
	return true;
}

/****************************************************************************
 * Name: up_txempty
 *
 * Description:
 *   Return true if the transmit fifo is empty
 *
 ****************************************************************************/

static bool rtl8721d_up_txempty(struct uart_dev_s *dev)
{
	struct rtl8721d_up_dev_s *priv = (struct rtl8721d_up_dev_s *)dev->priv;
	DEBUGASSERT(priv);
	while (!serial_writable(sdrv[uart_index_get(priv->Tx)]));
	return true;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef USE_SERIALDRIVER
/****************************************************************************
 * Name: up_serialinit
 *
 * Description:
 *   Register serial console and serial ports.  This assumes
 *   that up_earlyserialinit was called previously.
 *
 ****************************************************************************/

void up_serialinit(void)
{
	UART_WaitBusy(UART2_DEV, 100);
	UART_DeInit(UART2_DEV);
	UART_ClearRxFifo(UART2_DEV);
	UART_ClearTxFifo(UART2_DEV);
	CONSOLE_DEV.isconsole = true;
	rtl8721d_up_setup(&CONSOLE_DEV);

	/* Register the console */
	uart_register("/dev/console", &CONSOLE_DEV);

	/* Register all UARTs */
#ifdef TTYS0_DEV
	uart_register("/dev/ttyS0", &TTYS0_DEV);
#endif
#if 0//def TTYS1_DEV
	uart_register("/dev/ttyS1", &TTYS1_DEV);
#endif
#if 0//def TTYS2_DEV
	uart_register("/dev/ttyS2", &TTYS2_DEV);
#endif

}

/****************************************************************************
 * Name: up_lowputc
 *
 * Description:
 *   Output one byte on the serial console
 *
 * Input Parameters:
 *   ch - chatacter to output
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
void up_lowputc(char ch)
{
	LOGUART_PutChar(ch);
}

/****************************************************************************
 * Name: up_lowgetc
 *
 * Description:
 *   Read one byte from the serial console
 *
 * Input Parameters:
 *   none
 *
 * Returned Value:
 *   int value, -1 if error, 0~255 if byte successfully read
 *
 ****************************************************************************/
char up_lowgetc(void)
{
	uint8_t rxd;
	rxd = LOGUART_GetChar(_TRUE);
	return rxd & 0xff;
}

/****************************************************************************
 * Name: up_putc
 *
 * Description:
 *   Output one byte on the serial console
 *
 * Input Parameters:
 *   ch - chatacter to output
 *
 * Returned Value:
 *  sent character
 *
 ****************************************************************************/
int up_putc(int ch)
{
	/* Check for LF */

	if (ch == '\n') {
		/* Add CR */

		up_lowputc('\r');
	}

	up_lowputc(ch);
	return ch;
}

/****************************************************************************
 * Name: up_getc
 *
 * Description:
 *   Read one byte from the serial console
 *
 * Input Parameters:
 *   none
 *
 * Returned Value:
 *   int value, -1 if error, 0~255 if byte successfully read
 *
 ****************************************************************************/
int up_getc(void)
{
	int ch;
	ch = up_lowgetc();
	return ch;
}

#else							/* USE_SERIALDRIVER */
/****************************************************************************
 * Name: up_putc
 *
 * Description:
 *   Output one byte on the serial console
 *
 * Input Parameters:
 *   ch - chatacter to output
 *
 * Returned Value:
 *  sent character
 *
 ****************************************************************************/
int up_putc(int ch)
{
	/* Check for LF */

	if (ch == '\n') {
		/* Add CR */

		up_lowputc('\r');
	}
	up_lowputc(ch);

	return ch;
}

/****************************************************************************
 * Name: up_getc
 *
 * Description:
 *   Read one byte from the serial console
 *
 * Input Parameters:
 *   none
 *
 * Returned Value:
 *   int value, -1 if error, 0~255 if byte successfully read
 *
 ****************************************************************************/
int up_getc(void)
{
	int ch;
	ch = up_lowgetc();
	return ch;
}
#endif							/* USE_SERIALDRIVER */
