/*
 * (c) 2021 Ben Stragnell
 */

#include "rmiieth.h"
#include "rmiieth_md.h"
#include "pkt_utils.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include <stdio.h>
#include <stdlib.h>
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "lwip/ethip6.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/netifapi.h"
#include "lwip/ip_addr.h"
#include "lwip/acd.h"
#include "netif/ethernet.h"
#include "lwip/opt.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include "lwip/timeouts.h"
#include "lwip/apps/httpd.h"
#include "pkt_utils.h"
#include <string.h>

#define IFNAME0 'b'
#define IFNAME1 'b'

static uint8_t g_fake_mac[ 6 ] = {
    0xa4,0xdd,0x7b,0xb6,0xf2,0x1d
};



struct ethernetif {
    rmiieth_config* rmiieth_cfg;
};

static void ethernetif_input(struct netif *netif);

static void low_level_init(struct netif *netif)
{
    struct ethernetif *ethernetif = netif->state;
    struct pbuf *q;
    rmiieth_config* cfg = (rmiieth_config*)ethernetif->rmiieth_cfg;

    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    for( int i = 0 ; i < 6 ; i++ )
    {
        netif->hwaddr[ i ] = g_fake_mac[ i ];
    }
    netif->mtu = cfg->mtu;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
}

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    struct ethernetif *ethernetif = netif->state;
    struct pbuf *q;
    rmiieth_config* cfg = (rmiieth_config*)ethernetif->rmiieth_cfg;

#if ETH_PAD_SIZE
    pbuf_remove_header(p, ETH_PAD_SIZE); /* drop the padding word */
#endif

    //
    // compute required size
    //

    int cc_len = 0;
    for( q = p; q != NULL; q = q->next )
    {
        cc_len += q->len;
    }
    cc_len += 8;      // preamble
    cc_len += 4;
    if( cc_len < 72 ) // min length = 8 + 60 + 4 = 72
    {
        cc_len = 72;
    }

    //
    // allocate packet
    //

    uint8_t*    tx_buffer;
    int32_t     tx_len = 0;

    if( !rmiieth_tx_alloc_packet( cfg, cc_len, &tx_buffer) )
    {
        return( ERR_OK );           /// ?
    }

    //
    // construct TX packet, including preamble and fcs
    //

    tx_buffer[ tx_len++ ] = 0x55;  tx_buffer[ tx_len++ ] = 0x55;  tx_buffer[ tx_len++ ] = 0x55;  tx_buffer[ tx_len++ ] = 0x55;
    tx_buffer[ tx_len++ ] = 0x55;  tx_buffer[ tx_len++ ] = 0x55;  tx_buffer[ tx_len++ ] = 0x55;  tx_buffer[ tx_len++ ] = 0xd5;
    for( q = p; q != NULL; q = q->next )
    {
        memcpy( &tx_buffer[ tx_len ], q->payload, q->len );
        tx_len += q->len;
    }
    while( tx_len < 8 + 64 - 4 )
    {
        tx_buffer[ tx_len++ ] = 0x00;
    }
    uint32_t fcs = pkt_generate_fcs( &tx_buffer[ 8 ], tx_len - 8 );
    tx_buffer[ tx_len++ ] = (uint8_t)( fcs >>  0 );
    tx_buffer[ tx_len++ ] = (uint8_t)( fcs >>  8 );
    tx_buffer[ tx_len++ ] = (uint8_t)( fcs >> 16 );
    tx_buffer[ tx_len++ ] = (uint8_t)( fcs >> 24 );

    assert( cc_len == tx_len );

    rmiieth_tx_commit_packet( cfg, tx_len );

    MIB2_STATS_NETIF_ADD(netif, ifoutoctets, p->tot_len);
    if (((u8_t *)p->payload)[0] & 1) {
        /* broadcast or multicast packet*/
        MIB2_STATS_NETIF_INC(netif, ifoutnucastpkts);
    } else {
        /* unicast packet */
        MIB2_STATS_NETIF_INC(netif, ifoutucastpkts);
    }
    /* increase ifoutdiscards or ifouterrors on error */

#if ETH_PAD_SIZE
    pbuf_add_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
#endif

    LINK_STATS_INC(link.xmit);

    return ERR_OK;
}



static struct pbuf *low_level_input(struct netif *netif)
{
    struct ethernetif *ethernetif = netif->state;
    rmiieth_config* cfg = ethernetif->rmiieth_cfg;
    struct pbuf *p = NULL;
    struct pbuf *q;
    uint8_t*    pkt;
    int         pkt_len;

    if( !rmiieth_rx_get_packet( cfg, &pkt, &pkt_len ) )
    {
        return( NULL );
    }

    if( !pkt_validate( pkt, &pkt_len ) )
    {
        LINK_STATS_INC(link.drop);
        MIB2_STATS_NETIF_INC(netif, ifindiscards);
        rmiieth_rx_consume_packet( cfg );
        return( NULL );
    }

    p = pbuf_alloc(PBUF_RAW, pkt_len, PBUF_POOL);
    if( !p )
    {
        rmiieth_rx_consume_packet( cfg );
        return( NULL );
    }

    // we're good
    int pos = 0;
    for( q = p; q != NULL; q = q->next )
    {
        memcpy( q->payload, &pkt[ pos ], q->len );
        pos += q->len;
    }

    MIB2_STATS_NETIF_ADD(netif, ifinoctets, p->tot_len);
    if (((u8_t *)p->payload)[0] & 1) {
        MIB2_STATS_NETIF_INC(netif, ifinnucastpkts);
    } else {
        MIB2_STATS_NETIF_INC(netif, ifinucastpkts);
    }
    LINK_STATS_INC(link.recv);
    rmiieth_rx_consume_packet( cfg );
    return p;
}

static void ethernetif_input(struct netif *netif)
{
  struct ethernetif *ethernetif;
  struct eth_hdr *ethhdr;
  struct pbuf *p;

  ethernetif = netif->state;

  p = low_level_input(netif);
  if (p != NULL) {
    if (netif->input(p, netif) != ERR_OK) {
      LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
      pbuf_free(p);
      p = NULL;
    }
  }
}


err_t ethernetif_init(struct netif *netif)
{
  struct ethernetif *ethernetif;

  LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
  /* Initialize interface hostname */
  netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

  /*
   * Initialize the snmp variables and counters inside the struct netif.
   * The last argument should be replaced with your link speed, in units
   * of bits per second.
   */
  MIB2_INIT_NETIF(netif, snmp_ifType_ethernet_csmacd, LINK_SPEED_OF_YOUR_NETIF_IN_BPS);

  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;
  /* We directly use etharp_output() here to sethernetifave a function call.
   * You can instead declare your own function an call etharp_output()
   * from it if you have to do some checks before sending (e.g. if link
   * is available...) */
#if LWIP_IPV4
  netif->output = etharp_output;
#endif /* LWIP_IPV4 */
#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
#endif /* LWIP_IPV6 */
  netif->linkoutput = low_level_output;

//  ethernetif->ethaddr = (struct eth_addr *) & (netif->hwaddr[0]);

  /* initialize the hardware */
  low_level_init(netif);

  return ERR_OK;
}


//
//
//
//
//



void rmiieth_lwip_poll( struct netif* netif )
{
    struct ethernetif* ethernetif = netif->state;
    rmiieth_config* cfg = ethernetif->rmiieth_cfg;

    rmiieth_poll( cfg );
    while( rmiieth_rx_packet_available( cfg ) )
    {
        ethernetif_input( netif );
    }
}



void main_lwip( rmiieth_config* cfg )
{
    int rc;
    static struct netif rmiieth_netif;
    static struct ethernetif rmiieth_ethernetif;
    struct netif* nif;
    u8_t prevDHCPState = 0;

    lwip_init();


    memset( &rmiieth_ethernetif, 0, sizeof( rmiieth_ethernetif ) );
    rmiieth_ethernetif.rmiieth_cfg = cfg;
    rmiieth_netif.state = &rmiieth_ethernetif;
    nif = netif_add_noaddr( &rmiieth_netif, &rmiieth_ethernetif, ethernetif_init, ethernet_input );


    netif_set_up( &rmiieth_netif );
    netif_set_link_up( &rmiieth_netif );
    rc = dhcp_start( &rmiieth_netif );

    httpd_init();

    while( true )
    {
        sleep_us( 1 );
        sys_check_timeouts();
        rmiieth_lwip_poll( nif );

        // show link status periodically
        if( false )
        {
            static int ctr = 0;
            if( !(( ctr++ ) & 255 ) )
            {
                uint32_t v = rmiieth_md_readreg( cfg, RMII_REG_BASIC_STATUS );
                printf( "%08x\n", v );
            }
        }

        // show DHCP status
        struct dhcp* dd = netif_dhcp_data( &rmiieth_netif );
        if( dd->state != prevDHCPState )
        {
            printf( "DHCP State goes from %d to %d\n", prevDHCPState, dd->state );
            prevDHCPState = dd->state;
            if( dd->state == DHCP_STATE_BOUND )
            {
                char    tmp[ 256 ];
                printf( " Got address: \n" );
                printf( "   IP     : %s\n", ipaddr_ntoa_r( &dd->offered_ip_addr, tmp, sizeof( tmp ) ) );
                printf( "   Subnet : %s\n", ipaddr_ntoa_r( &dd->offered_sn_mask, tmp, sizeof( tmp ) ) );
                printf( "   GW     : %s\n", ipaddr_ntoa_r( &dd->offered_gw_addr, tmp, sizeof( tmp ) ) );
            }
        }

    }

}



int main( int argc, char** argv )
{
    rmiieth_config     rmii_cfg;

    //
    // set system clock
    //

    set_sys_clock_khz( 250000, false );
    setup_default_uart();
    printf( "hello world\n" );

    //
    // init rmiieth interface
    //

    rmiieth_set_default_config( &rmii_cfg );
    rmiieth_init( &rmii_cfg );
    if( !rmiieth_probe( &rmii_cfg ) )
    {
        assert( false );
    }
    sleep_ms( 500 );
    rmiieth_md_reset( &rmii_cfg );
    sleep_ms( 500 );

    //
    // select 100Mbit ethernet, duplex, autonegotiation
    //

    //  basic control:          0011 0011 0000 0000         enable auto-neg, restart auto-neg
    //  autoneg-advert:         0000 0001 1000 0001         100Mbps, full duplex

    uint32_t basic_control = 0x3300;
    rmiieth_md_writereg( &rmii_cfg, RMII_REG_AUTONEG_ADVERT, 0x181 );
    rmiieth_md_writereg( &rmii_cfg, RMII_REG_BASIC_CONTROL, basic_control );
    sleep_ms( 500 );

    //
    // lwip startup
    //

    if( true )
    {
        main_lwip( &rmii_cfg );
    }

    return( 0 );
}


