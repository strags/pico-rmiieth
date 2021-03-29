/*
 * (c) 2021 Ben Stragnell
 */

#include "rmiieth_md.h"


#define RMII_ST         ( 1 << 30 )
#define RMII_OP_READ    ( 2 << 28 )
#define RMII_OP_WRITE   ( 1 << 28 )
#define RMII_PHY_SHIFT  ( 23)
#define RMII_REG_SHIFT  ( 18)
#define RMII_TA_SHIFT   ( 16)
#define RMII_TA         ( 2 << RMII_TA_SHIFT )





void rmiieth_md_init( rmiieth_config* cfg )
{
    // setup rpio for md interface
    gpio_init( cfg->pin_mdc );
    gpio_put( cfg->pin_mdc, 1 );
    gpio_set_dir( cfg->pin_mdc, GPIO_OUT );
    gpio_init( cfg->pin_mdio );
    gpio_set_dir( cfg->pin_mdio, GPIO_IN );
}

void RMII_MDIO_startRead( rmiieth_config* cfg )
{
    gpio_set_dir( cfg->pin_mdio, GPIO_IN );
}

void RMII_MDIO_startWrite( rmiieth_config* cfg )
{
    gpio_set_dir( cfg->pin_mdio, GPIO_OUT );
}

void RMII_MDIO_writeBit( rmiieth_config* cfg, int bit )
{
    gpio_put( cfg->pin_mdc, 0 );
    gpio_put( cfg->pin_mdio, bit );
    sleep_us( cfg->sleep_us );
    gpio_put( cfg->pin_mdc, 1 );
    sleep_us( cfg->sleep_us );
}

int RMII_MDIO_readBit( rmiieth_config* cfg )
{
    int bit;
    gpio_put( cfg->pin_mdc, 0 );
    sleep_us( cfg->sleep_us );
    bit = gpio_get( cfg->pin_mdio );
    gpio_put( cfg->pin_mdc, 1 );
    sleep_us( cfg->sleep_us );
    return( bit );
}

void RMII_MDIO_writeBits( rmiieth_config* cfg, uint32_t v, int bitCt )
{
    for( int i = 0 ; i < bitCt ; i++ )
    {
        RMII_MDIO_writeBit( cfg, ( v >> ( bitCt - 1 ) ) & 1 );
        v <<= 1;
    }
}

uint32_t RMII_MDIO_readBits( rmiieth_config* cfg, int bitCt )
{
    uint32_t v = 0;
    for( int i = 0 ; i < bitCt ; i++ )
    {
        v <<= 1;
        v |= RMII_MDIO_readBit( cfg );
    }
    return( v );
}

uint32_t rmiieth_md_readreg( rmiieth_config* cfg, uint32_t regAddr )
{
    uint32_t        v;
    RMII_MDIO_startWrite( cfg );
    RMII_MDIO_writeBits( cfg, 0xffffffff, 32 );
    v = RMII_ST | RMII_OP_READ | ( cfg->phy_addr << RMII_PHY_SHIFT ) | ( regAddr << RMII_REG_SHIFT );
    v >>= 18;
    RMII_MDIO_writeBits( cfg, v, 14 );
    RMII_MDIO_startRead( cfg );
    RMII_MDIO_readBits( cfg, 2 );
    v = RMII_MDIO_readBits( cfg, 16 );
    return( v );
}

void rmiieth_md_writereg( rmiieth_config* cfg, uint32_t regAddr, uint32_t regVal )
{
    uint32_t        v;
    RMII_MDIO_startWrite( cfg );
    RMII_MDIO_writeBits( cfg, 0xffffffff, 32 );
    v = RMII_ST | RMII_OP_WRITE | ( cfg->phy_addr << RMII_PHY_SHIFT ) | ( regAddr << RMII_REG_SHIFT ) | RMII_TA | regVal;
    RMII_MDIO_writeBits( cfg, v, 32 );
}


void rmiieth_md_reset( rmiieth_config* cfg )
{
    uint32_t    v = rmiieth_md_readreg( cfg, RMII_REG_BASIC_CONTROL );
    v |= 0x80000000;
    rmiieth_md_writereg( cfg, RMII_REG_BASIC_CONTROL, v );
}
