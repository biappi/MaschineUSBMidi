//
//  midi-state-machine.c
//  simple-maschine-midi
//
//  Created by Antonio Malara on 07/04/2020.
//  Copyright © 2020 Antonio Malara. All rights reserved.
//

#include "midi-state-machine.h"
#include <stdio.h>

void midi_parser_init(midi_parser * parser, midi_parser_callback *callback) {
    parser->state = midi_parser_wait_for_status;
    parser->send = callback;
}

static int next_state_for_byte(uint8_t byte, midi_parser_state *next) {
    switch (byte & 0xf0) {
        case 0x80: // note off
        case 0x90: // note on
        case 0xa0: // aftertouch
        case 0xb0: // controller
        case 0xe0: // pitch wheel
            *next = midi_parser_receive_1st_data_byte;
            return 1;
            
        case 0xc0: // program change
        case 0xd0: // channel pressure
            *next = midi_parser_receive_data_byte;
            return 1;
            
        case 0xf0: // realtime messages
            switch (byte) {
                case 0xf0:
                    *next = midi_parser_receive_sysex;
                    return 1;
                    
                case 0xf1: // time code
                case 0xf3: // song select
                    *next = midi_parser_receive_data_byte;
                    return 1;
                    
                case 0xf2: // song position
                    *next = midi_parser_receive_1st_data_byte;
                    return 1;
            }
    }
    
    return 0;
}

void midi_parser_parse(midi_parser * parser, uint8_t byte) {
    midi_parser_state next;

    if (next_state_for_byte(byte, &next)) {
        parser->len = 1;
        parser->packet[0] = byte;
        parser->state = next;
    }

    else {
        switch (parser->state) {
            case midi_parser_wait_for_status:
                break;
                
            case midi_parser_receive_1st_data_byte:
                parser->packet[1] = byte;
                parser->state = midi_parser_receive_2nd_data_byte;
                break;
                
            case midi_parser_receive_2nd_data_byte: {
                parser->packet[2] = byte;
                parser->state = midi_parser_receive_1st_data_byte;
                parser->send(parser->packet, 3);
                break;
            }
                
            case midi_parser_receive_data_byte: {
                parser->packet[1] = byte;
                parser->send(parser->packet, 2);
                break;
            }
                
            case midi_parser_receive_sysex: {
                parser->packet[parser->len] = byte;
                parser->len++;
                
                if (byte == 0xf7) {
                    parser->send(parser->packet, parser->len);
                    parser->state = midi_parser_wait_for_status;
                }
                
                break;
            }                

        }
    }
}
