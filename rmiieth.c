/*
 * (c) 2021 Ben Stragnell
 */

#include "rmiieth.h"
#include "rmiieth_md.h"
#include "rmii_ext_clk.pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pkt_utils.h"
#include <string.h>

static void rmiieth_rx_irq_handler( void );
static void rmiieth_rx_try_start( rmiieth_config* cfg );
static void rmiieth_start_tx( rmiieth_config* cfg, pkt_queue_pkt* p );

static rmiieth_config* g_cfg;


void rmiieth_set_default_config( rmiieth_config* cfg )
{
    memset( cfg, 0, sizeof( rmiieth_config ) );
    cfg->pio = pio0;
    cfg->pin_mdc = 14;
    cfg->pin_mdio = 15;
    cfg->pin_clk = 10;
    cfg->pin_rx_base = 11;
    cfg->pin_rx_valid = 13;
    cfg->pin_tx_base = 7;
    cfg->pin_tx_valid = 9;
    cfg->sleep_us = 1000;
    cfg->phy_addr = 0x01;            // this happens to be the default
    cfg->rx_dma_chan = 0;
    cfg->tx_dma_chan = 1;
    cfg->rx_irq = 0;
    cfg->rx_lock_id = -1;

    cfg->rx_queue_buffer_size = 8192;
    cfg->tx_queue_buffer_size = 8192;
    cfg->mtu = 1500;
}

bool rmiieth_probe( rmiieth_config* cfg )
{
    printf( "PROBING\n" ); 
    for( int i = 0 ; i < 0x20 ; i++ )
    {
        cfg->phy_addr = i;
        uint32_t vv = rmiieth_md_readreg( cfg, RMII_REG_BASIC_STATUS );
        printf( "%d -> %08x\n", i, vv );
        if( vv != 0xffff )
        {
            return( true );
        }
    }
    return( false );
}

void rmiieth_init( rmiieth_config* cfg )
{
    dma_channel_config      c;

    g_cfg = cfg;

    //
    // init the MD interface
    //

    rmiieth_md_init( cfg );

    //
    // init spinlocks
    //

    if( cfg->rx_lock_id < 0 )
    {
        cfg->rx_lock_id = next_striped_spin_lock_num();
    }
    spin_lock_claim( cfg->rx_lock_id );
    cfg->rx_lock = spin_lock_init( cfg->rx_lock_id );

    //
    // init buffers
    //

    if( !cfg->rx_queue_buffer )
    {
        cfg->rx_queue_buffer = (uint8_t*)malloc( cfg->rx_queue_buffer_size );
        if( !cfg->rx_queue_buffer )
        {
            assert( false );
        }
    }
    pkt_queue_init( &cfg->rx_queue, cfg->rx_queue_buffer, cfg->rx_queue_buffer_size );
    if( !cfg->tx_queue_buffer )
    {
        cfg->tx_queue_buffer = (uint8_t*)malloc( cfg->tx_queue_buffer_size );
        if( !cfg->rx_queue_buffer )
        {
            assert( false );
        }
    }
    pkt_queue_init( &cfg->tx_queue, cfg->tx_queue_buffer, cfg->tx_queue_buffer_size );

    //
    // init IRQ - the RX state machine interrupts us at the end of a packet
    //

    int pio_irq = cfg->pio == pio0 ? PIO0_IRQ_0 : PIO1_IRQ_0;
    pio_irq += cfg->rx_irq;

    irq_set_exclusive_handler( pio_irq, rmiieth_rx_irq_handler );
    irq_set_enabled( pio_irq, true );
    cfg->pio->inte0 = cfg->rx_irq ? PIO_IRQ1_INTE_SM0_BITS : PIO_IRQ0_INTE_SM0_BITS;
//    irq_set_exclusive_handler( PIO0_IRQ_0, rmiieth_rx_irq_handler );
//    irq_set_enabled( PIO0_IRQ_0, true );
//    cfg->pio->inte0 = PIO_IRQ0_INTE_SM0_BITS;


    //
    // init the pins
    //

    pio_gpio_init( cfg->pio, cfg->pin_clk );
    pio_gpio_init( cfg->pio, cfg->pin_rx_base );
    pio_gpio_init( cfg->pio, cfg->pin_rx_base + 1 );
    pio_gpio_init( cfg->pio, cfg->pin_rx_valid );
    pio_gpio_init( cfg->pio, cfg->pin_tx_base );
    pio_gpio_init( cfg->pio, cfg->pin_tx_base + 1 );
    pio_gpio_init( cfg->pio, cfg->pin_tx_valid );

    //
    // allocate state-machines
    //

    if( ( cfg->clk_sm = pio_claim_unused_sm( cfg->pio, false ) ) < 0 )
    {
        assert( false );
    }
    if( ( cfg->rx_sm = pio_claim_unused_sm( cfg->pio, false ) ) < 0 )
    {
        assert( false );
    }
    if( ( cfg->tx_sm = pio_claim_unused_sm( cfg->pio, false ) ) < 0 )
    {
        assert( false );
    }


    //
    // initialize the clk program
    //

    cfg->clk_offset = pio_add_program( cfg->pio, &clk_program );
    cfg->clk_config = clk_program_get_default_config( cfg->clk_offset );
    sm_config_set_in_pins( &cfg->clk_config, cfg->pin_clk );
    pio_sm_set_consecutive_pindirs( cfg->pio, cfg->clk_sm, cfg->pin_clk, 1, false );
    pio_sm_init( cfg->pio, cfg->clk_sm, cfg->clk_offset, &cfg->clk_config );
    pio_sm_set_enabled( cfg->pio, cfg->clk_sm, true );

    //
    // init the RX program
    //

    cfg->rx_offset = pio_add_program( cfg->pio, &rx_program );
    cfg->rx_config = rx_program_get_default_config( cfg->rx_offset );
    sm_config_set_in_pins( &cfg->rx_config, cfg->pin_rx_base );
    sm_config_set_in_shift( &cfg->rx_config, true, true, 32 );
    sm_config_set_jmp_pin( &cfg->rx_config, cfg->pin_rx_valid );
    pio_sm_set_consecutive_pindirs( cfg->pio, cfg->rx_sm, cfg->pin_rx_base, 2, false );
    pio_sm_set_consecutive_pindirs( cfg->pio, cfg->rx_sm, cfg->pin_rx_valid, 1, false );
    pio_sm_init( cfg->pio, cfg->rx_sm, cfg->rx_offset, &cfg->rx_config );

    //
    // init RX DMA
    //

    c = dma_channel_get_default_config( cfg->rx_dma_chan );
    channel_config_set_read_increment( &c, false );
    channel_config_set_write_increment( &c, true );
    channel_config_set_dreq( &c, pio_get_dreq( cfg->pio, cfg->rx_sm, false ) );
    channel_config_set_transfer_data_size( &c, DMA_SIZE_32 );
    dma_channel_configure(
        cfg->rx_dma_chan,
        &c,
        NULL,
        (void*)((uintptr_t)( &cfg->pio->rxf[ cfg->rx_sm ] ) ),
        1,
        false
    );

    //
    // init the TX program
    //

    cfg->tx_offset = pio_add_program( cfg->pio, &tx_program );
    cfg->tx_config = tx_program_get_default_config( cfg->tx_offset );
    sm_config_set_out_pins( &cfg->tx_config, cfg->pin_tx_base, 2 );
    sm_config_set_out_shift( &cfg->tx_config, true, true, 32 );
    sm_config_set_set_pins( &cfg->tx_config, cfg->pin_tx_base, 2 );
    sm_config_set_sideset_pins( &cfg->tx_config, cfg->pin_tx_valid );
    pio_sm_set_consecutive_pindirs( cfg->pio, cfg->tx_sm, cfg->pin_tx_base, 2, true);
    pio_sm_set_consecutive_pindirs( cfg->pio, cfg->tx_sm, cfg->pin_tx_valid, 1, true );
    pio_sm_init( cfg->pio, cfg->tx_sm, cfg->tx_offset, &cfg->tx_config );
//    pio_sm_set_clkdiv( cfg->pio, cfg->tx_sm, 100.0 );
    pio_sm_set_enabled( cfg->pio, cfg->tx_sm, true );

    //
    // init TX DMA
    //

    c = dma_channel_get_default_config( cfg->tx_dma_chan );
    channel_config_set_read_increment( &c, true );
    channel_config_set_write_increment( &c, false );
    channel_config_set_dreq( &c, pio_get_dreq( cfg->pio, cfg->tx_sm, true ) );
    channel_config_set_transfer_data_size( &c, DMA_SIZE_32 );
    dma_channel_configure(
        cfg->tx_dma_chan,
        &c,
        (void*)((uintptr_t)( &cfg->pio->txf[ cfg->tx_sm ] ) ),
        NULL,
        1,
        false
    );


}

static void rmiieth_start_tx( rmiieth_config* cfg, pkt_queue_pkt* p )
{
    cfg->tx_current_pkt = p;

    // init SM
//    pio_sm_init( cfg->pio, cfg->tx_sm, cfg->tx_offset, &cfg->tx_config );

    // send bit-pair count
    int bit_pair_ct = ( p->hdr.data_bytes << 2 );

    // send extra padding bytes
    int extra_bytes = 4 - ( p->hdr.data_bytes & 3 );

    // fill in the 8 bytes that we reserved before the packet data....
    uint32_t*       pctrs = (uint32_t *)p->data;
    pctrs[ 0 ] = bit_pair_ct - 1;
    pctrs[ 1 ] = extra_bytes - 1;

    printf( "TX: %d bytes\n", p->hdr.data_bytes );
#if PKT_DEBUG_PRINTS
    pkt_dump( p->data, ( p->hdr.data_bytes + extra_bytes + 8 ), 2048 );
#endif

    dma_channel_set_read_addr( cfg->tx_dma_chan, p->data, false );
    dma_channel_set_trans_count( cfg->tx_dma_chan, ( p->hdr.data_bytes + extra_bytes + 8 ) >> 2, true );
}

void rmiieth_poll( rmiieth_config* cfg )
{
    // if we filled the current packet, the DMA will have halted - fake an interrupt
    {
        uint32_t ii = spin_lock_blocking( cfg->rx_lock );
        if( cfg->rx_current_pkt && !dma_channel_is_busy( cfg->rx_dma_chan ) )
        {
            printf( "*** buffer overrun\n" );
            rmiieth_rx_irq_handler();
        }
        spin_unlock( cfg->rx_lock, ii );
    }

    // if the RX process has not yet started, or it stalled out due to lack of space in the RX queue,
    // attempt to restart it
    if( !cfg->rx_current_pkt )
    {
        uint32_t ii = spin_lock_blocking( cfg->rx_lock );
        rmiieth_rx_try_start( cfg );
        spin_unlock( cfg->rx_lock, ii );
    }

    // consider starting a new TX
    if( !dma_channel_is_busy( cfg->tx_dma_chan ) )
    {
        if( cfg->tx_current_pkt )
        {
            pkt_queue_consume_pkt( &cfg->tx_queue );
            cfg->tx_current_pkt = NULL;
        }

        pkt_queue_pkt*      p;
        p = pkt_queue_peek_pkt( &cfg->tx_queue );
        if( p )
        {
            rmiieth_start_tx( cfg, p );
        }
    }


}

bool rmiieth_rx_packet_available( rmiieth_config* cfg )
{
    pkt_queue_pkt* pkt = pkt_queue_peek_pkt( &cfg->rx_queue );
    return( pkt && pkt != cfg->rx_current_pkt );
}

bool rmiieth_rx_get_packet( rmiieth_config* cfg, uint8_t** pkt_data, int* length )
{
    pkt_queue_pkt* pkt = pkt_queue_peek_pkt( &cfg->rx_queue );
    if( !pkt || pkt == cfg->rx_current_pkt )
    {
        return( false );
    }
    *pkt_data = pkt->data;
    *length = pkt->hdr.data_bytes;
    return( true );
}

void rmiieth_rx_consume_packet( rmiieth_config* cfg )
{
    uint32_t ii = spin_lock_blocking( cfg->rx_lock );
    pkt_queue_consume_pkt( &cfg->rx_queue );
    spin_unlock( cfg->rx_lock, ii );
}

bool rmiieth_tx_alloc_packet( rmiieth_config* cfg, int length, uint8_t** data )
{
    assert( !cfg->tx_current_alloc_pkt );

    // allocate 8 extra bytes at front, for the bit/byte counters
    
    cfg->tx_current_alloc_pkt = pkt_queue_reserve_pkt( &cfg->tx_queue, length + 8 );
    if( !cfg->tx_current_alloc_pkt )
    {
        return( false );
    }
    *data = cfg->tx_current_alloc_pkt->data + 8;
    return( true );
}

bool rmiieth_tx_commit_packet( rmiieth_config* cfg, int length )
{
    assert( cfg->tx_current_alloc_pkt );
    pkt_queue_commit_pkt( &cfg->tx_queue, cfg->tx_current_alloc_pkt, length );
    cfg->tx_current_alloc_pkt = NULL;
}

static void __time_critical_func(rmiieth_rx_irq_handler)( void )
{
    rmiieth_config*     cfg = g_cfg;

    // halt the dma
    dma_channel_abort( cfg->rx_dma_chan );

    // read the write address, compute the # of bytes received
    uintptr_t write_addr = dma_channel_hw_addr( cfg->rx_dma_chan )->write_addr;
    int32_t bytes = ( write_addr - (uintptr_t)(cfg->rx_current_pkt->data) );
    pkt_queue_commit_pkt( &cfg->rx_queue, cfg->rx_current_pkt, bytes );
    cfg->rx_current_pkt = NULL;

    // clear PIO irq
    cfg->pio->irq = 0x01;

    // try to start a new transfer
    rmiieth_rx_try_start( cfg );
}

// NOTE: must hold rx spinlock on entry to this function
static void __time_critical_func(rmiieth_rx_try_start)( rmiieth_config* cfg )
{
    if( cfg->rx_current_pkt )
    {
        return;
    }

    cfg->rx_current_pkt = pkt_queue_reserve_pkt( &cfg->rx_queue, ( cfg->mtu + 52 ) & (~3) );
    if( !cfg->rx_current_pkt )
    {
        return;
    }

    pio_sm_init( cfg->pio, cfg->rx_sm, cfg->rx_offset, &cfg->rx_config );

    // restart the DMA
    dma_channel_set_write_addr( cfg->rx_dma_chan, cfg->rx_current_pkt->data, false );
    dma_channel_set_trans_count( cfg->rx_dma_chan, cfg->rx_current_pkt->hdr.data_bytes >> 2, true );

    // restart the state machine
    pio_sm_set_enabled( cfg->pio, cfg->rx_sm, true );
}

