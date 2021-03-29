/*
 * (c) 2021 Ben Stragnell
 */

#ifndef RMIIETH_MD_H
#define RMIIETH_MD_H

#include "rmiieth.h"

#define RMII_REG_BASIC_CONTROL          ( 0 )
#define RMII_REG_BASIC_STATUS           ( 1 )
#define RMII_REG_PHY_ID1                ( 2 )
#define RMII_REG_PHY_ID2                ( 3 )
#define RMII_REG_AUTONEG_ADVERT         ( 4 )
#define RMII_REG_AUTONEG_LP_ABILITY     ( 5 )
#define RMII_REG_AUTONEG_EXP            ( 6 )
#define RMII_REG_MODE_CS                ( 17 )
#define RMII_REG_SPECIAL_MODES          ( 18 )
#define RMII_REG_SYMBOL_ERR_CTR         ( 26 )
#define RMII_REG_CS_INDICATION          ( 27 )
#define RMII_REG_INT_SRC                ( 29 )
#define RMII_REG_INT_MSK                ( 30 )
#define RMII_REG_PHY_SPECIAL_CS         ( 31 )


extern void     rmiieth_md_init( rmiieth_config* cfg );
extern uint32_t rmiieth_md_readreg( rmiieth_config* cfg, uint32_t regAddr );
extern void     rmiieth_md_writereg( rmiieth_config* cfg, uint32_t regAddr, uint32_t regVal );
extern void     rmiieth_md_reset( rmiieth_config* cfg );


#endif

