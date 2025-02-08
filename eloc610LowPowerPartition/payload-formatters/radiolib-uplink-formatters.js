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
    var msgType;
    msgType = input.bytes[0];
    var batterySoC, timestamp, recordingState;
    var state
    switch(msgType) {
        case 0:     // status messages
            timestamp = (input.bytes[1] << 24 | input.bytes[2] << 16 | input.bytes[3] << 8 | input.bytes[4]);
            var date = new Date(timestamp * 1000);
            batterySoC = input.bytes[5];
            switch (input.bytes[6]) {
                case 0:
                    recordingState = "recordOff_detectOff";
                    break;
                case 1:
                    recordingState = "recordOn_detectOff";
                    break;
                case 2:
                    recordingState = "recordOn_detectOn";
                    break;
                case 3:
                    recordingState = "recordOff_detectOn";
                    break;
                case 4:
                    recordingState = "recordOnEvent";
                    break;
                default:
                    recordingState = "recInvalid";
                    break;
            }
            return {
                data: {
                msgType: msgType,
                time: date,
                state: recordingState,
                Battery: batterySoC,
                bytes: input.bytes
                },
                warnings: [],
                errors: []
            };
          break;
        case 1: // event notfier
          // code block
          break;
        default:
          // code block
      } 

    return {
        data: {
        msgType: msgType,
        bytes: input.bytes
        },
        warnings: [],
        errors: []
    };
}