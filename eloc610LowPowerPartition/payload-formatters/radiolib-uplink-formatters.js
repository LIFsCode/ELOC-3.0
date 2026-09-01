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
        case 2: // intruder alarm (knock-based intruder detection + GPS position)
            timestamp = 0
            for (let i = 0; i < 8; i++) {  // retrieve 64 bit timestamp
                timestamp = timestamp * 256 + input.bytes[idx++];
            }
            var date = timeConverter(timestamp);
            var flags = input.bytes[idx++];
            var hasFix = (flags & 0x01) != 0;
            // bit1 (firmware >= 1.70): the device is still being moved. When clear it has been
            // still for a few minutes, so it reports at the slower idleIntervalS with the GPS
            // powered down and lat/lng from its last known fix - check fixAgeS.
            var moving = (flags & 0x02) != 0;
            // int32 big endian, degrees * 1e5 (signed)
            var latRaw = ((input.bytes[idx++] << 24) | (input.bytes[idx++] << 16) |
                          (input.bytes[idx++] << 8)  |  input.bytes[idx++]) | 0;
            var lngRaw = ((input.bytes[idx++] << 24) | (input.bytes[idx++] << 16) |
                          (input.bytes[idx++] << 8)  |  input.bytes[idx++]) | 0;
            batterySoC = input.bytes[idx++];
            var fixAgeS = (input.bytes[idx++] << 8) | input.bytes[idx++];

            return {
                data: {
                msgType: msgType,
                msgVers: msgVers,
                time: date,
                timestamp: timestamp,
                intruderAlarm: true,
                hasFix: hasFix,
                moving: moving,
                latitude: hasFix ? latRaw / 100000.0 : null,
                longitude: hasFix ? lngRaw / 100000.0 : null,
                fixAgeS: hasFix ? fixAgeS : null,
                Battery: batterySoC,
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