#ifndef _RADIOLIB_EX_LORAWAN_CONFIG_H
#define _RADIOLIB_EX_LORAWAN_CONFIG_H


// joinEUI - previous versions of LoRaWAN called this AppEUI
// for development purposes you can use all zeros - see wiki for details
#define RADIOLIB_LORAWAN_JOIN_EUI  0x0000000000000000

// the Device EUI & two keys can be generated on the TTN console 
#ifndef RADIOLIB_LORAWAN_DEV_EUI   // Replace with your Device EUI
#define RADIOLIB_LORAWAN_DEV_EUI   0x70B3D57ED0062B25
#endif
#ifndef RADIOLIB_LORAWAN_APP_KEY   // Replace with your App Key 
#define RADIOLIB_LORAWAN_APP_KEY   0xA5, 0xB4, 0x7F, 0xA2, 0x67, 0x47, 0x25, 0x1A, 0x40, 0x9A, 0xA1, 0xA9, 0x67, 0xA3, 0x5A, 0xA5 
#endif
#ifndef RADIOLIB_LORAWAN_NWK_KEY   // Put your Nwk Key here
#define RADIOLIB_LORAWAN_NWK_KEY   0x1A, 0x7C, 0xFA, 0x01, 0x7A, 0x21, 0x04, 0x4B, 0xE7, 0xDB, 0xC8, 0xAD, 0x1F, 0x6E, 0xE4, 0x1A 
#endif


// ============================================================================
// Below is to support the sketch - only make changes if the notes say so ...







#endif
