//
//  midi-state-machine.h
//  simple-maschine-midi
//
//  Created by Antonio Malara on 07/04/2020.
//  Copyright © 2020 Antonio Malara. All rights reserved.
//

#ifndef midi_state_machine_h
#define midi_state_machine_h

#include <stdint.h>

typedef enum {
    midi_parser_wait_for_status,
    
    midi_parser_receive_1st_data_byte,
    midi_parser_receive_2nd_data_byte,
    
    midi_parser_receive_data_byte,
    
    midi_parser_receive_sysex,
} midi_parser_state;

typedef void (midi_parser_callback)(uint8_t *buf, int len, void *user_data);

typedef struct {
    midi_parser_state state;
    midi_parser_callback *send;
    uint8_t packet[1024];
    int len;
    void *user_data;
} midi_parser;

void midi_parser_init(midi_parser * parser, midi_parser_callback *callback, void *user_data);
void midi_parser_parse(midi_parser * parser, uint8_t byte);

#endif /* midi_state_machine_h */
