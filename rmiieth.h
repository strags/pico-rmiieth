/*
 * (c) 2021 Ben Stragnell
 */

#ifndef RMIIETH_H
#define RMIIETH_H

#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pkt_queue.h"

typedef struct
{
    // initial config
    PIO             pio;                                    // which PIO to use
    int             pin_mdc;                                // MD clock pin
    int             pin_mdio;                               // MD data pin
    int             sleep_us;                               // time to sleep between MD bits
    uint32_t        phy_addr;                               // phy_addr - or use rmiieth_probe() to auto-probe
    int             pin_clk;                                // refclk input pin
    int             pin_tx_base;                            // TX0 (TX1 must be adjacent)
    int             pin_tx_valid;                           // TX_EN
    int             pin_rx_base;                            // RX0 (RX1 must be adjacent)
    int             pin_rx_valid;                           // CRS
    int             rx_dma_chan;                            // RX dma channel id
    int             tx_dma_chan;                            // TX dma channel id
    int             rx_lock_id;                             // spinlock id - leave as -1 to auto-allocate
    uint8_t*        rx_queue_buffer;                        // either pass in a buffer, or NULL to have rmiieth_init() malloc one
    int32_t         rx_queue_buffer_size;                   // RX queue size
    uint8_t*        tx_queue_buffer;                        // either pass in a buffer, or NULL to have rmiieth_init() malloc one
    int32_t         tx_queue_buffer_size;                   // TX queue size
    int             rx_irq;                                 // RX IRQ number
    int             mtu;                                    // max packet size

    // state
    uint8_t         clk_offset;
    pio_sm_config   clk_config;
    int             clk_sm;
    uint8_t         rx_offset;
    pio_sm_config   rx_config;
    int             rx_sm;
    uint8_t         tx_offset;
    pio_sm_config   tx_config;
    int             tx_sm;
    spin_lock_t*    rx_lock;                                // spinlock for accessing RX queue
    pkt_queue       rx_queue;                               // the RX queue
    pkt_queue_pkt*  rx_current_pkt;                         // RX packet currently being received (NULL if stalled)
    pkt_queue       tx_queue;                               // the TX queue
    pkt_queue_pkt*  tx_current_pkt;                         // TX packet currently being transmitted
    pkt_queue_pkt*  tx_current_alloc_pkt;                   // TX packet currently allocated

} rmiieth_config;



extern void rmiieth_set_default_config( rmiieth_config* cfg );
extern bool rmiieth_probe( rmiieth_config* cfg );
extern void rmiieth_init( rmiieth_config* cfg );
extern void rmiieth_poll( rmiieth_config* cfg );
extern bool rmiieth_rx_packet_available( rmiieth_config* cfg );
extern bool rmiieth_rx_get_packet( rmiieth_config* cfg, uint8_t** pkt, int* length );
extern void rmiieth_rx_consume_packet( rmiieth_config* cfg );
extern bool rmiieth_tx_alloc_packet( rmiieth_config* cfg, int length, uint8_t** data );
extern bool rmiieth_tx_commit_packet( rmiieth_config* cfg, int length );



#endif 