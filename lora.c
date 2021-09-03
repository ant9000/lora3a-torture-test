#include "thread.h"
#include "net/netdev.h"
#include "net/netdev/lora.h"

#include "sx127x_internal.h"
#include "sx127x_params.h"
#include "sx127x_netdev.h"

#include "common.h"

#define SX127X_LORA_MSG_QUEUE   (32U)
#define SX127X_STACKSIZE        (THREAD_STACKSIZE_DEFAULT)
#define MSG_TYPE_ISR            (0x3456)

static char stack[SX127X_STACKSIZE];
static kernel_pid_t lora_recv_pid;
static sx127x_t sx127x;
static char lora_buffer[MAX_PACKET_LEN];
static netdev_lora_rx_info_t lora_packet_info;

static lora_data_cb_t *lora_data_cb;
static void _lora_rx_cb(netdev_t *dev, netdev_event_t event);
void *_lora_recv_thread(void *arg);

int lora_init(const lora_state_t *state)
{
    lora_data_cb = state->data_cb;

    sx127x.params = sx127x_params[0];
    netdev_t *netdev = (netdev_t *)&sx127x;
    netdev->driver = &sx127x_driver;

    spi_init(sx127x.params.spi);
    if (netdev->driver->init(netdev) < 0) return 1;

    uint8_t  lora_bw = state->bandwidth;
    uint8_t  lora_sf = state->spreading_factor;
    uint8_t  lora_cr = state->coderate;
    uint32_t lora_ch = state->channel;
    int16_t  lora_pw = state->power;

    netdev->driver->set(netdev, NETOPT_BANDWIDTH, &lora_bw, sizeof(lora_bw));
    netdev->driver->set(netdev, NETOPT_SPREADING_FACTOR, &lora_sf, sizeof(lora_sf));
    netdev->driver->set(netdev, NETOPT_CODING_RATE, &lora_cr, sizeof(lora_cr));
    netdev->driver->set(netdev, NETOPT_CHANNEL_FREQUENCY, &lora_ch, sizeof(lora_ch));
    netdev->driver->set(netdev, NETOPT_TX_POWER, &lora_pw, sizeof(lora_pw));

    netdev->event_callback = _lora_rx_cb;

    lora_recv_pid = thread_create(stack, sizeof(stack), THREAD_PRIORITY_MAIN - 1,
                              THREAD_CREATE_STACKTEST, _lora_recv_thread, NULL,
                              "_lora_recv_thread");

    if (lora_recv_pid <= KERNEL_PID_UNDEF) return 1;
    return 0;
}

void lora_off(void)
{
    sx127x_set_sleep(&sx127x);
    spi_release(sx127x.params.spi);
    spi_deinit_pins(sx127x.params.spi);
}

int lora_write(char *msg, size_t len)
{
    iolist_t payload = {
        .iol_base = msg,
        .iol_len = len,
    };
    netdev_t *netdev = (netdev_t *)&sx127x;
    // wait for radio to stop transmitting
    while (netdev->driver->send(netdev, &payload) == -ENOTSUP) { ztimer_sleep(ZTIMER_USEC, 10); }
    // wait for end of transmission, adding a few usecs to time on air
    uint32_t delay = sx127x_get_time_on_air(&sx127x, iolist_size(&payload));
    ztimer_sleep(ZTIMER_USEC, delay * 1000 + 10);

    return len;
}

void lora_listen(void)
{
    netdev_t *netdev = (netdev_t *)&sx127x;
    const netopt_enable_t single = false;
    netdev->driver->set(netdev, NETOPT_SINGLE_RECEIVE, &single, sizeof(single));
    const uint32_t timeout = 0;
    netdev->driver->set(netdev, NETOPT_RX_TIMEOUT, &timeout, sizeof(timeout));
    netopt_state_t state = NETOPT_STATE_RX;
    netdev->driver->set(netdev, NETOPT_STATE, &state, sizeof(state));
}

static void _lora_rx_cb(netdev_t *dev, netdev_event_t event)
{
    if (event == NETDEV_EVENT_ISR) {
        msg_t msg;
        msg.type = MSG_TYPE_ISR;
        msg.content.ptr = dev;
        if (msg_send(&msg, lora_recv_pid) <= 0) {
           /* possibly lost interrupt */
        }
    } else {
        size_t len;
        switch (event) {
            case NETDEV_EVENT_RX_COMPLETE:
                len = dev->driver->recv(dev, NULL, 0, 0);
                if (len <= MAX_PACKET_LEN) {
                    dev->driver->recv(dev, lora_buffer, len, &lora_packet_info);
                    lora_data_cb(lora_buffer, len, &lora_packet_info.rssi, &lora_packet_info.snr);
                } else {
                    /* spurious communication - ignore */
                }
                break;
            case NETDEV_EVENT_TX_COMPLETE:
                lora_listen();
                break;
            default:
                break;
        }
    }
}

void *_lora_recv_thread(void *arg)
{
    (void)arg;
    static msg_t _msg_q[SX127X_LORA_MSG_QUEUE];
    msg_init_queue(_msg_q, SX127X_LORA_MSG_QUEUE);
    while (1) {
        msg_t msg;
        msg_receive(&msg);
        if (msg.type == MSG_TYPE_ISR) {
            netdev_t *dev = msg.content.ptr;
            dev->driver->isr(dev);
        }
    }
}
