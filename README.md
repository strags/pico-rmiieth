# pico-rmiieth
100Mbit/sec RMII interface for Raspberry Pi Pico

Should work with most off-the-shelf LAN8720 modules. (Just google "Amazon LAN8720" to find a bunch of near-identical compatible modules).


## Usage

The code, as provided, contains a **main.c** that initializes the library, and uses LWIP to obtain an IP address via DHCP, and serve up a simple static file.


To use the library directly, you need to do the following things:

1. Initialize a rmii_config structure:

```
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
```

You can, if you like, call:

    void rmiieth_set_default_config( rmiieth_config* cfg )

to set sensible defaults. The library consumes pretty much all the program memory and 3 state machines on a single PIO.

### Hardware

#### GPIO connections

The RMII module requires 9 pins to be connected to the Pico:

* MDIO Interface

    These signals can be assigned to any GPIO

        * MD Clock
        * MDIO Data

* Clock

    50MHz refclk from the RMII module.
    Could be any of gpio 20, 22, 12 or 14.

* TX Pins

        * TX0       (pin_tx_base)
        * TX1
        * TX_EN     (pin_tx_valid)

    The TX0 and TX1 pins must be adjacent and in order (PIN_TX1 = PIN_TX0 + 1).

* RX Pins

        * RX0       (pin_rx_base)
        * RX1
        * CRS       (pin_rx_valid)

    The RX0 and RX1 pins must be adjacent and in order (PIN_RX1 = PIN_RX0 + 1).
    
### Software configuration

#### Packet queues
If you wish, you can pass in pre-allocated buffers for the packet queues, or you can leave the xx_queue_buffer pointer set to NULL, and simply specify a size - in order to have the rmiieth code allocate the buffers for you. The size of the queues depends on (a) how much data you expect to receive/transmit, and (b) the period of time between successive calls to ```rmiieth_poll```.

#### PHY address
The LAN8720 module is capable of being assigned 32 different addresses. The default on my module appears to be 1. However, you can also call ```rmiieth_probe``` to try and auto-discover the address of the attached device (by reading MD status registers).





2. Call ```rmiieth_init``` with the prepared config.


3. Periodically call ```rmiieth_poll``` - this will cause the next queued TX packet to start transmission (assuming one is ready to go, and the TX channel is idle). It will also restart RX requests if the RX channel is not yet started, or has aborted due to a too-large packet.

4. To send a packet, do this:

```
    uint8_t*    pkt_data;
    int         pkt_size = 128;         // sufficient size of packet + 8-byte preamble + 4-byte FSC
    if( rmiieth_tx_alloc_packet( cfg, pkt_size, &pkt_data ) )
    {
        // ... fill in packet (including 8-byte 55 55 55 ... d5 preamble and frame sequence check) ...
        rmiieth_tx_commit_packet( cfg, pkt_size );      // if you want, you can shrink the final packet here
    }
```

5. To check for received packets, do this:

```
    if( rmiieth_rx_packet_available( cfg ) )
    {
        uint8_t*        pkt_data;
        int             pkt_length;
        if( rmiieth_rx_get_packet( cfg, &pkt_data, &pkt_length ) )
        {
            // process the packet
            rmiieth_rx_consume_packet( cfg );
        }
    }
```

### Notes

In order to receive and transmit clocked packet data with sufficient accuracy, it's necessary to overclock the Pico to 250MHz. This has been absolutely fine with every Pico I've tested it with, but of course YMMV.

