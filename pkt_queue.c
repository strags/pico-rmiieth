/*
 * (c) 2021 Ben Stragnell
 */

#include "pkt_queue.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/platform.h"

void pkt_queue_init( pkt_queue* pq, uint8_t* data, int32_t size )
{
    pq->data = data;
    pq->head = NULL;
    pq->tail = NULL;
    pq->size = size;
}

pkt_queue_pkt* __time_critical_func( pkt_queue_reserve_pkt )( pkt_queue* pq, int32_t max_size )
{
    int32_t             required_bytes = ( max_size + sizeof( pkt_queue_pkt_hdr ) + 3 ) & ( ~3 );
    pkt_queue_pkt*    pkt;

    // empty queue?
    if( !pq->tail )
    {
        if( required_bytes > pq->size )
        {
            return( NULL );
        }
        pkt = (pkt_queue_pkt*)pq->data;
        pkt->hdr.data_bytes = max_size;
        pkt->hdr.mem_bytes = required_bytes;
        pq->head = pkt;
        pq->tail = pkt;
        return( pkt );
    }

    int32_t             rpos = ( (uint8_t*)pq->head ) - pq->data;
    int32_t             wpos = ( (uint8_t*)pq->tail ) - pq->data;
    wpos = ( wpos + pq->tail->hdr.mem_bytes ) % pq->size;

    // alloc on RHS?
    if( rpos <= wpos )
    {
        // need to wrap?
        if( required_bytes <= ( pq->size - wpos ) )
        {
            pkt = (pkt_queue_pkt*)( &pq->data[ wpos ] );
            pkt->hdr.mem_bytes = required_bytes;
            pkt->hdr.data_bytes = max_size;
            pq->tail = pkt;
            return( pkt );
        }

        // can wrap?
        if( required_bytes >= rpos )
        {
            return( NULL );
        }
        pq->tail->hdr.mem_bytes += ( pq->size - wpos );
        wpos = 0;
    }

    // can alloc on LHS?
    if( required_bytes > ( rpos - wpos ) )
    {
        return( NULL );
    }

    pkt = (pkt_queue_pkt*)( &pq->data[ wpos ] );
    pkt->hdr.mem_bytes = required_bytes;
    pkt->hdr.data_bytes = max_size;
    pq->tail = pkt;
    return( pkt );
}

void __time_critical_func(pkt_queue_commit_pkt)( pkt_queue* pq, pkt_queue_pkt* pkt, int32_t actual_size )
{
    assert( pq->tail == pkt );
    assert( actual_size <= pkt->hdr.data_bytes  );
    pkt->hdr.mem_bytes -= ( pkt->hdr.data_bytes - actual_size ) & (~3);
    pkt->hdr.data_bytes = actual_size;
}

pkt_queue_pkt* pkt_queue_peek_pkt( pkt_queue* pq )
{
    return( pq->head );
}

void pkt_queue_consume_pkt( pkt_queue* pq )
{
    if( !pq->head )
    {
        return;
    }
    if( pq->head == pq->tail )
    {
        pq->head = NULL;
        pq->tail = NULL;
        return;
    }

    int32_t             rpos = ( (uint8_t*)pq->head ) - pq->data;
    rpos = ( rpos + pq->head->hdr.mem_bytes ) % pq->size;
    pq->head = (pkt_queue_pkt*)( &pq->data[ rpos ] );
}

void pkt_queue_dump( pkt_queue* pq )
{
    pkt_queue_pkt*        pkt = pq->head;

    printf( "\nQueue:\n" );
    if( !pkt )
    {
        printf( "<empty>\n" );
        return;
    }

    while( 1 )
    {
        int32_t pos = ( (uint8_t*)pkt ) - pq->data;
        printf( "%-5d -> %-5d [%-5d] : %d bytes\n", pos, pos + pkt->hdr.mem_bytes, pkt->hdr.mem_bytes, pkt->hdr.data_bytes );
        if( pkt == pq->tail )
        {
            break;
        }
        pos = ( pos + pkt->hdr.mem_bytes ) % pq->size;
        pkt = (pkt_queue_pkt*)( &pq->data[ pos ] );
    }
}

uint8_t         g_test_buffer[ 4096 ];
pkt_queue g_test_queue;

void pkt_queue_test( void )
{
    pkt_queue*            pq = &g_test_queue;
    pkt_queue_pkt*            pkt;

    pkt_queue_init( pq, g_test_buffer, sizeof( g_test_buffer ) );
    pkt_queue_dump( pq );

    pkt = pkt_queue_reserve_pkt( pq, 1016 );
    pkt_queue_dump( pq );
    pkt = pkt_queue_reserve_pkt( pq, 1016 );
    pkt_queue_dump( pq );
    pkt = pkt_queue_reserve_pkt( pq, 1016 );
    pkt_queue_dump( pq );
    pkt = pkt_queue_reserve_pkt( pq, 1016 );
    pkt_queue_dump( pq );
    
    pkt_queue_commit_pkt( pq, pkt, 500 );
    pkt_queue_dump( pq );

    pkt = pkt_queue_reserve_pkt( pq, 500 );
    pkt_queue_dump( pq );

    pkt_queue_consume_pkt( pq );
    pkt_queue_dump( pq );

    pkt = pkt_queue_reserve_pkt( pq, 500 );
    pkt_queue_dump( pq );


    pkt = pkt_queue_reserve_pkt( pq, 1600 );
    pkt_queue_dump( pq );

    pkt = pkt_queue_reserve_pkt( pq, 1600 );
    pkt_queue_dump( pq );
    pkt_queue_commit_pkt( pq, pkt, 800 );

    pkt = pkt_queue_reserve_pkt( pq, 1600 );
    pkt_queue_dump( pq );

    pkt_queue_consume_pkt( pq );
    pkt_queue_dump( pq );

    pkt = pkt_queue_reserve_pkt( pq, 1500 );
    pkt_queue_dump( pq );

    pkt = pkt_queue_reserve_pkt( pq, 1600 );
    pkt_queue_dump( pq );

    pkt = pkt_queue_reserve_pkt( pq, 50 );
    pkt_queue_dump( pq );

    pkt_queue_consume_pkt( pq );
    pkt_queue_dump( pq );
    pkt_queue_consume_pkt( pq );
    pkt_queue_dump( pq );
    pkt_queue_consume_pkt( pq );
    pkt_queue_dump( pq );
    pkt_queue_consume_pkt( pq );
    pkt_queue_dump( pq );

    for( int i = 0 ; i < 100000 ; i++ )
    {
        int32_t     sz = ( rand() & 0x3ff ) + 0x400;

        pkt = pkt_queue_reserve_pkt( pq, sz );
        if( pkt )
        {
            pkt_queue_commit_pkt( pq, pkt, sz / 2 );
        }
        else
        {
            pkt_queue_consume_pkt( pq );
        }

        if( !( i % 1000 ) )
        {
            pkt_queue_dump( pq );
        }
    }

    while( pkt_queue_peek_pkt( pq ) )
    {
        pkt_queue_consume_pkt( pq );
    }

    pkt_queue_dump( pq );
}

