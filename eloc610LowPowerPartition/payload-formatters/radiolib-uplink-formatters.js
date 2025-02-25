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

// note these parameters MUST match with the Eloc Lora implementation
const LORA_LABEL_LEN = 5



function timeConverter(UNIX_timestamp){
    var a = new Date(UNIX_timestamp * 1000);
    var months = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];
    var year = a.getFullYear();
    var month = months[a.getMonth()];
    var date = a.getDate();
    var hour = a.getHours();
    var min = a.getMinutes();
    var sec = a.getSeconds();
    var time = date + ' ' + month + ' ' + year + ' ' + hour + ':' + min + ':' + sec ;
    return time;
}

function bin2String(array) {
    var result = "";
    for (var i = 0; i < array.length; i++) {
        result += String.fromCharCode(array[i]);
    }
    return result;
}

function decodeUplink(input) {
    var idx=0;
    var msgType, msgVers;
    msgType = (input.bytes[idx++] & 0xF0) >> 4;
    msgVers = input.bytes[0] & 0x0F;
    // TODO: check msgVersion for incompatibility
    var batterySoC, timestamp, recordingState;
    var state
    switch(msgType) {
        case 0:     // status messages
            timestamp = 0
            for (let i = 0; i < 8; i++) {  // retrieve 64 bit timestamp
                timestamp = timestamp | (input.bytes[idx++] << (64 -(i+1)*8));
            }
            var date = timeConverter(timestamp);
            batterySoC = input.bytes[idx++];
            switch (input.bytes[idx++]) {
                case 0:
                    recordingState = "recInvalid";
                    break;
                case 1:
                    recordingState = "recordOff_detectOff";
                    break;
                case 2:
                    recordingState = "recordOn_detectOff";
                    break;
                case 3:
                    recordingState = "recordOn_detectOn";
                    break;
                case 4:
                    recordingState = "recordOff_detectOn";
                    break;
                case 5:
                    recordingState = "recordOnEvent";
                    break;
                default:
                    recordingState = "recInvalid";
                    break;
            }
            return {
                data: {
                msgType: msgType,
                msgVers: msgVers,
                time: date,
                timestamp: timestamp,
                state: recordingState,
                Battery: batterySoC,
                bytes: input.bytes
                },
                warnings: [],
                errors: []
            };
          break;
        case 1: // event notfier
            timestamp = 0
            for (let i = 0; i < 8; i++) {  // retrieve 64 bit timestamp
                timestamp = timestamp | (input.bytes[idx++] << (64 -(i+1)*8));
            }
            var date = timeConverter(timestamp);
            var numEvents = input.bytes[idx++];
            var events = [];
            for (let i = 0; i < numEvents; i++) {  // retrieve 64 bit timestamp
                //var label = bin2String(input.bytes.subarray(idx, idx+LORA_LABEL_LEN));
                //idx += LORA_LABEL_LEN;
                var chars = []
                for(var c = 0; c<LORA_LABEL_LEN; c++) {
                    chars[c] = input.bytes[idx++];
                }
                var label = bin2String(chars);
                var confidence = input.bytes[idx++];
                events[i] = {label : label, confidence : confidence};
            }
            
            return {
                data: {
                msgType: msgType,
                msgVers: msgVers,
                time: date,
                timestamp: timestamp,
                numEvents: numEvents,
                events: events,
                bytes: input.bytes
                },
                warnings: [],
                errors: []
            };
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