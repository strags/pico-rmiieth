/*
 * (c) 2021 Ben Stragnell
 */

#include <stdio.h>
#include <stdint.h>
#include "pkt_utils.h"

static uint32_t g_grc_table[] =
{
    0x4DBDF21C, 0x500AE278, 0x76D3D2D4, 0x6B64C2B0,
    0x3B61B38C, 0x26D6A3E8, 0x000F9344, 0x1DB88320,
    0xA005713C, 0xBDB26158, 0x9B6B51F4, 0x86DC4190,
    0xD6D930AC, 0xCB6E20C8, 0xEDB71064, 0xF0000000
};

bool pkt_remove_preamble( uint8_t* pkt, int* pkt_len_ptr )
{
    int                 len = *pkt_len_ptr;
    uint32_t            sync = 0;
    
    for( int i = 0 ; i < len ; i++ )
    {
        uint8_t     byte;
        byte = pkt[ i ];
        for( int j = 0; j < 8 ; j+=2 )
        {
            if( sync == 0xaaaaaaab )
            {
                int nl = 0;
                for( int k = i ; k < len-1 ; k++ )
                {
                    pkt[ nl++ ] = ( pkt[ k+1 ] << ( 8 - j ) ) | ( pkt[ k ] >> j );
                }
                *pkt_len_ptr = nl;
                return( true );
            }
            sync <<= 2;
            sync |= ( byte & 1 ) << 1;
            sync |= ( byte & 2 ) >> 1;
            byte >>= 2;
        }
    }
    return( false );
}

int pkt_generate_fcs_and_determine_length( uint8_t* data, int max_length )
{
    uint32_t    crc = 0;
    uint32_t    next_bytes;

    next_bytes = ( (uint32_t)data[ 0 ] <<  0 ) |
                 ( (uint32_t)data[ 1 ] <<  8 ) |
                 ( (uint32_t)data[ 2 ] << 16 ) |
                 ( (uint32_t)data[ 3 ] << 24 );

    for( uint32_t i = 4 ; i <= max_length ; i++ )
    {
        if( crc == next_bytes )
        {
            return( i - 4 );
        }
        uint8_t     nb = next_bytes & 0xff;
        crc = (crc >> 4) ^ g_grc_table[ ( crc ^ ( nb >> 0 ) ) & 0x0F ];
        crc = (crc >> 4) ^ g_grc_table[ ( crc ^ ( nb >> 4 ) ) & 0x0F ];
        next_bytes >>= 8;
        next_bytes |= ((uint32_t)data[ i ]) << 24;
    }
    return( -1 );
}

uint32_t pkt_generate_fcs( uint8_t* data, int length )
{
    uint32_t crc = 0;
    for( uint32_t i = 0 ; i < length ; i++ )
    {
        crc = (crc >> 4) ^ g_grc_table[ ( crc ^ ( data[ i ] >> 0 ) ) & 0x0F ];
        crc = (crc >> 4) ^ g_grc_table[ ( crc ^ ( data[ i ] >> 4 ) ) & 0x0F ];
    }
    return( crc );
}


bool pkt_validate( uint8_t* pkt, int* pkt_len_ptr )
{
    if( !pkt_remove_preamble( pkt, pkt_len_ptr ) )
    {
#if PKT_DEBUG_PRINTS
        printf( "failed to find preamble/sfd\n" );
        pkt_dump( pkt, *pkt_len_ptr, 64 );
#endif        
        return( false );
    }

    int             len = *pkt_len_ptr;
    if( len < 4 )
    {
        return( false );
    }

    {
        int q = pkt_generate_fcs_and_determine_length( pkt, len );
        if( q < 0 )
        {
#if PKT_DEBUG_PRINTS
            printf( "Unable to match FCS\n" );
            pkt_dump( pkt, *pkt_len_ptr, 2048 );
#endif
            return( false );
        }
        *pkt_len_ptr = q;
    }

#if PKT_DEBUG_PRINTS || 1
    printf( "Received valid packet of %d bytes\n", *pkt_len_ptr );
#endif

    return( true );
}

void pkt_dump( uint8_t* pkt, int len, int max_len )
{
    if( len > max_len )
    {
        len = max_len;
    }
    for( int i = 0 ; i < len ; i++ )
    {
        printf( "%02x ", pkt[ i ] );
    }
    printf( "\n" );
}