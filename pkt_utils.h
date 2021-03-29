/*
 * (c) 2021 Ben Stragnell
 */

#ifndef PKT_UTILS_H_INCLUDED
#define PKT_UTILS_H_INCLUDED

#include <stdio.h>
#include <stdint.h>
#include <pico/stdlib.h>

#define PKT_DEBUG_PRINTS        0

bool        pkt_remove_preamble( uint8_t* pkt, int* pkt_len_ptr );
int         pkt_generate_fcs_and_determine_length( uint8_t* data, int max_length );
uint32_t    pkt_generate_fcs( uint8_t* data, int length );
bool        pkt_validate( uint8_t* pkt, int* pkt_len_ptr );
void        pkt_dump( uint8_t* pkt, int len, int max_len );

#endif // #ifndef PKT_UTILS_H_INCLUDED