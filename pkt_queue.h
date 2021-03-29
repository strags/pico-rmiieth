/*
 * (c) 2021 Ben Stragnell
 */

#ifndef PKT_QUEUE_INCLUDED
#define PKT_QUEUE_INCLUDED

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * pkt_queue
 * 
 * This is a circular queue structure that, when you attempt to reserve space that would normally wrap, pads the previous packet,
 * and starts allocating from offset 0 - thus guaranteeing that all reserved buffers are contiguous.
 * 
 * It also allows you to truncate the current reservation - so that you can reserve the max packet-size for DMA, and then truncate
 * when you know how big the received packet actually is.
 * 
 * It is expected that functions are called in this order:
 * 
 *      pkt_queue_init()
 * 
 * Then, on the write side:
 * 
 *      pkt_queue_reserve_pkt()   - reserves space - returns NULL if none available
 *      pkt_queue_commit_pkt()    - optional, only necessary if you are truncating the current packet
 *
 * On the read side:
 * 
 *      pkt_queue_peek_pkt()      - returns the next available packet for reading (or NULL if the queue is empty)
 *      pkt_queue_consume_pkt()   - consumes the current packet, and release the space
 * 
 * Note: If there's one reserved packet in the queue, and you're DMAing into it, pkt_queue_peek_pkt() will still return it.
 * If the packet is in use, you need to account for that.
 * 
 */

typedef struct pkt_queue_pkt_hdr
{
    int32_t                 data_bytes;
    int32_t                 mem_bytes;
} pkt_queue_pkt_hdr;

typedef struct pkt_queue_pkt
{
    pkt_queue_pkt_hdr   hdr;
    uint8_t             data[ 1 ];
} pkt_queue_pkt;

typedef struct pkt_queue
{
    pkt_queue_pkt*      head;
    pkt_queue_pkt*      tail;
    int32_t             size;
    uint8_t*            data;
} pkt_queue;



void pkt_queue_init( pkt_queue* pq, uint8_t* data, int32_t size );
pkt_queue_pkt* pkt_queue_reserve_pkt( pkt_queue* pq, int32_t max_size );
void pkt_queue_commit_pkt( pkt_queue* pq, pkt_queue_pkt* pkt, int32_t actual_size );
pkt_queue_pkt* pkt_queue_peek_pkt( pkt_queue* pq );
void pkt_queue_consume_pkt( pkt_queue* pq );


void pkt_queue_dump( pkt_queue* pq );
void pkt_queue_test( void );


#endif // #ifndef PKT_QUEUE_INCLUDED