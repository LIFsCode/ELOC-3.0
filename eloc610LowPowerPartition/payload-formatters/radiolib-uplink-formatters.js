/*******************************************************************************
 *
 *  File:         lmic-node-uplink-formatters.js
 * 
 *  Function:     LMIC-node uplink payload formatter JavaScript function(s).
 * 
 *  Author:       LIFsCode
 * 
 *  Description:  These function(s) are for use with The Things Network V3. 
 *                 
 ******************************************************************************/

function decodeUplink(input) {
    var val1, val2;
    val1 = input.bytes[0];
    val2 = (input.bytes[1] << 8 | input.bytes[2]);
    return {
        data: {
        counter1: val1,
        counter2: val2,
        bytes: input.bytes
        },
        warnings: [],
        errors: []
    };
}