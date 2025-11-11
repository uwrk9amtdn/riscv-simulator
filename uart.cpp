#include "uart.h"

#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>
#include <poll.h>

#define UART_RBR 0
#define UART_THR 0

#define UART_IER       1
#define UART_IER_ERBFI 0x01
#define UART_IER_ETBEI 0x02
#define UART_IER_ELSI  0x04
#define UART_IER_EDSSI 0x08

#define UART_IIR        2
#define UART_IIR_NO_INT 0x01

#define UART_FCR             2
#define UART_FCR_ENABLE_FIFO 0x01
#define UART_FCR_CLEAR_RCVR  0x02
#define UART_FCR_CLEAR_XMIT  0x04

#define UART_LCR      3
#define UART_LCR_DLAB 0x80

#define UART_MCR      4
#define UART_MCR_LOOP 0x10

#define UART_LSR      5
#define UART_LSR_DR   0x01
#define UART_LSR_OE   0x02
#define UART_LSR_PE   0x04
#define UART_LSR_FE   0x08
#define UART_LSR_BI   0x10
#define UART_LSR_THRE 0x20
#define UART_LSR_TEMT 0x40
#define UART_LSR_ERR  0x80

#define UART_MSR     6
#define UART_MSR_CTS 0x10
#define UART_MSR_DSR 0x20
#define UART_MSR_RI  0x40
#define UART_MSR_DCD 0x80

#define UART_SCR 7

#define UART_QUEUE_SIZE 16

uart::uart(const char* tty, ::plic* plic, u32 interrupt_id)
{
    this->plic_ = plic;
    this->interrupt_id = interrupt_id;

    rbr = 0;
    thr = 0;
    ier = 0;
    iir = UART_IIR_NO_INT;
    fcr = 0;
    lcr = 0;
    mcr = 0;
    lsr = UART_LSR_THRE | UART_LSR_TEMT;
    msr = UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS;
    scr = 0;
    dll = 0;
    dlm = 0;

    if (tty) {
        custom_tty = true;
        fd = open(tty, O_RDWR | O_NOCTTY);
        if (fd < 0) {
            perror("Failed to open tty");
            throw std::runtime_error("UART initialization failed");
        }
    } else {
        custom_tty = false;
        fd = 0;
        if (tcgetattr(fd, &old_tios) == 0) {
            termios new_tios = old_tios;
            new_tios.c_lflag &= ~(ICANON | ECHO);
            if (tcsetattr(0, TCSANOW, &new_tios) != 0) {
                perror("Failed to set terminal attributes");
                throw std::runtime_error("UART initialization failed");
            }
        } else {
            perror("Failed to get terminal attributes");
            throw std::runtime_error("UART initialization failed");
        }
    }

    update_interrupt();
}

uart::~uart()
{
    if (custom_tty) {
        close(fd);
    } else {
        if (tcsetattr(0, TCSANOW, &old_tios) != 0) {
            perror("Failed to restore terminal attributes");
        }
    }
}

bool uart::load(u32 addr, u32 len, u8* data)
{
    u8 val;
    bool update = false;

    if (len != 1 || addr > size() - 1 || data == 0) {
        return false;
    }

    switch (addr) {
    case UART_RBR:
        if (lcr & UART_LCR_DLAB) {
            val = dll;
        } else {
            update = true;
            if (fcr & UART_FCR_ENABLE_FIFO) {
                if (rx_queue.empty()) {
                    val = 0;
                    break;
                }

                val = rx_queue.front();
                rx_queue.pop();

                if (rx_queue.empty()) {
                    lsr &= ~UART_LSR_DR;
                }
            } else {
                val = rbr;
                lsr &= ~UART_LSR_DR;
            }
        }
        break;

    case UART_IER:
        if (lcr & UART_LCR_DLAB) {
            val = dlm;
        } else {
            val = ier;
        }
        break;

    case UART_IIR:
        val = iir;
        break;

    case UART_LCR:
        val = lcr;
        break;

    case UART_MCR:
        val = mcr;
        break;

    case UART_LSR:
        val = lsr;
        lsr &= ~(UART_LSR_OE | UART_LSR_PE | UART_LSR_FE | UART_LSR_BI);
        update = true;
        break;

    case UART_MSR:
        val = msr;
        break;

    case UART_SCR:
        val = scr;
        break;

    default:
        return false;
        break;
    }

    data[0] = val;

    if (update) {
        update_interrupt();
    }

    return true;
}

bool uart::store(u32 addr, u32 len, const u8* data)
{

    u8 val;
    bool update = false;

    if (len != 1 || addr > size() - 1 || data == 0) {
        return false;
    }

    val = data[0];

    switch (addr) {
    case UART_THR:
        if (lcr & UART_LCR_DLAB) {
            dll = val;
            break;
        } else {
            update = true;
            if (mcr & UART_MCR_LOOP) {
                if (fcr & UART_FCR_ENABLE_FIFO) {
                    if (rx_queue.size() < UART_QUEUE_SIZE) {
                        rx_queue.push(val);
                    } else {
                        lsr |= UART_LSR_OE;
                    }
                } else {
                    rbr = val;
                    if (lsr & UART_LSR_DR) {
                        lsr |= UART_LSR_OE;
                    }
                }

                lsr |= UART_LSR_DR;
            } else {
                ::write(fd == 0 ? 1 : fd, &val, 1);
            }
            break;
        }

    case UART_IER:
        if (lcr & UART_LCR_DLAB) {
            dlm = val;
        } else {
            update = true;
            ier = val & 0x0f;
        }
        break;

    case UART_FCR:
        update = true;
        if ((fcr ^ val) & UART_FCR_ENABLE_FIFO) {
            while (!rx_queue.empty()) {
                rx_queue.pop();
            }
            lsr &= ~UART_LSR_DR;
        }

        if (val & UART_FCR_ENABLE_FIFO) {
            iir |= 0b11000000;
            fcr = val & 0b11000001;
            if (val & UART_FCR_CLEAR_RCVR) {
                while (!rx_queue.empty()) {
                    rx_queue.pop();
                }
                lsr &= ~UART_LSR_DR;
            }
        } else {
            iir &= ~0b11000000;
        }

        break;

    case UART_LCR:
        lcr = val;
        break;

    case UART_MCR:
        mcr = val & 0x1f;
        break;

    case UART_LSR:
        // read only
        break;

    case UART_MSR:
        // read only
        break;

    case UART_SCR:
        scr = val;
        break;

    default:
        return false;
        break;
    }

    if (update) {
        update_interrupt();
    }

    return true;
}

u32 uart::size() const
{
    return 8;
}

void uart::tick()
{
    struct pollfd pfd;
    int ret;
    u8 ch;

    if (mcr & UART_MCR_LOOP) {
        return;
    }

    pfd.fd = fd;
    pfd.events = POLLIN;

    ret = poll(&pfd, 1, 0);
    if (ret <= 0 || !(pfd.revents & POLLIN)) {
        return;
    }

    ret = ::read(fd, &ch, 1);
    if (ret <= 0) {
        return;
    }

    if (fcr & UART_FCR_ENABLE_FIFO) {
        if (rx_queue.size() < UART_QUEUE_SIZE) {
            rx_queue.push(ch);
        } else {
            lsr |= UART_LSR_OE;
        }
    } else {
        rbr = ch;
        if (lsr & UART_LSR_DR) {
            lsr |= UART_LSR_OE;
        }
    }

    lsr |= UART_LSR_DR;

    update_interrupt();
}

void uart::update_interrupt()
{
    iir = (iir & 0b11110000) | 0b00000001;

    if (ier & UART_IER_ELSI) {
        if (lsr & UART_LSR_OE) {
            iir = (iir & 0b11110000) | 0b00000110;
            goto _end;
        }
    }

    if (ier & UART_IER_ERBFI) {
        bool trigger = false;
        if (fcr & UART_FCR_ENABLE_FIFO) {
            int l = 0;
            switch (fcr & 0b11000000) {
            case 0b00000000:
                l = 1;
                break;
            case 0b01000000:
                l = 4;
                break;
            case 0b10000000:
                l = 8;
                break;
            case 0b11000000:
                l = 14;
                break;
            }
            if (rx_queue.size() >= l) {
                trigger = true;
            }
        } else {
            if (lsr & UART_LSR_DR) {
                trigger = true;
            }
        }
        if (trigger) {
            iir = (iir & 0b11110000) | 0b00000100;
            goto _end;
        }
    }

    if (ier & UART_IER_ETBEI) {
        iir = (iir & 0b11110000) | 0b00000010;
        goto _end;
    }

_end:

    bool i = (iir & 0x01) ? false : true;
    plic_->set_interrupt_signal(interrupt_id, i);
}