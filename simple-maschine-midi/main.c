//
//  main.c
//  simple-maschine-midi
//
//  Created by Antonio Malara on 24/11/2019.
//  Copyright © 2019 Antonio Malara. All rights reserved.
//

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <libusb/libusb.h>
#include <CoreMIDI/CoreMIDI.h>

#include "midi-state-machine.h"
#include "controls-map.h"

const uint16_t USB_VID_NATIVEINSTRUMENTS  = 0x17cc;
const uint16_t USB_PID_MASCHINECONTROLLER = 0x0808;

libusb_device_handle * maschine;
midi_parser parser;

MIDIClientRef client;
MIDIEndpointRef source;
MIDIEndpointRef destination;

enum EP1_COMMANDS {
    EP1_CMD_GET_DEVICE_INFO = 0x1,
    EP1_CMD_READ_ERP        = 0x2,
    EP1_CMD_READ_ANALOG     = 0x3,
    EP1_CMD_READ_IO         = 0x4,
    EP1_CMD_WRITE_IO        = 0x5,
    EP1_CMD_MIDI_READ       = 0x6,
    EP1_CMD_MIDI_WRITE      = 0x7,
    EP1_CMD_AUDIO_PARAMS    = 0x9,
    EP1_CMD_AUTO_MSG        = 0xb,
    EP1_CMD_DIMM_LEDS       = 0xc,
};

struct caiaq_device_spec {
    uint16_t fw_version;
    uint8_t  hw_subtype;
    uint8_t  num_erp;
    uint8_t  num_analog_in;
    uint8_t  num_digital_in;
    uint8_t  num_digital_out;
    uint8_t  num_analog_audio_out;
    uint8_t  num_analog_audio_in;
    uint8_t  num_digital_audio_out;
    uint8_t  num_digital_audio_in;
    uint8_t  num_midi_out;
    uint8_t  num_midi_in;
    uint8_t  data_alignment;
} __attribute__ ((packed));

static unsigned int decode_erp(uint8_t a, uint8_t b)
{
    /* some of these devices have endless rotation potentiometers
     * built in which use two tapers, 90 degrees phase shifted.
     * this algorithm decodes them to one single value, ranging
     * from 0 to 999 */
    
    static const int HIGH_PEAK = 268;
    static const int LOW_PEAK  = -7;

    static const int range     = HIGH_PEAK - LOW_PEAK;
    
    static const int DEG90     = (range / 2);
    static const int DEG180    = (range);
    static const int DEG270    = (DEG90 + DEG180);
    static const int DEG360    = (DEG180 * 2);
    
    int weight_a, weight_b;
    int pos_a, pos_b;
    int ret;
    int mid_value = (HIGH_PEAK + LOW_PEAK) / 2;

    weight_b = abs(mid_value - a) - (range / 2 - 100) / 2;

    if (weight_b < 0)
        weight_b = 0;

    if (weight_b > 100)
        weight_b = 100;

    weight_a = 100 - weight_b;

    if (a < mid_value) {
        /* 0..90 and 270..360 degrees */
        pos_b = b - LOW_PEAK + DEG270;
        if (pos_b >= DEG360)
            pos_b -= DEG360;
    } else
        /* 90..270 degrees */
        pos_b = HIGH_PEAK - b + DEG90;


    if (b > mid_value)
        /* 0..180 degrees */
        pos_a = a - LOW_PEAK;
    else
        /* 180..360 degrees */
        pos_a = HIGH_PEAK - a + DEG180;

    /* interpolate both slider values, depending on weight factors */
    /* 0..99 x DEG360 */
    ret = pos_a * weight_a + pos_b * weight_b;

    /* normalize to 0..999 */
    ret *= 10;
    ret /= DEG360;

    if (ret < 0)
        ret += 1000;

    if (ret >= 1000)
        ret -= 1000;

    return ret;
}

static void ep1_command_responses_callback(struct libusb_transfer * transfer) {
    
    enum EP1_COMMANDS cmd = transfer->buffer[0];
    
    switch (cmd) {
        case EP1_CMD_GET_DEVICE_INFO:
        {
            struct caiaq_device_spec *
            reply = (struct caiaq_device_spec *)&(transfer->buffer[1]);
            
            reply = reply;
            
            break;
        }
            
        case EP1_CMD_READ_ERP:
        {
            uint8_t * buf = transfer->buffer + 1;
            
            /* 4 under the left screen */
            printf("%4d ", decode_erp(buf[21], buf[20]));
            printf("%4d ", decode_erp(buf[15], buf[14]));
            printf("%4d ", decode_erp(buf[9],  buf[8]));
            printf("%4d ", decode_erp(buf[3],  buf[2]));
            printf("\n");
            
            /* 4 under the right screen */
            printf("%4d ", decode_erp(buf[19], buf[18]));
            printf("%4d ", decode_erp(buf[13], buf[12]));
            printf("%4d ", decode_erp(buf[7],  buf[6]));
            printf("%4d ", decode_erp(buf[1],  buf[0]));
            printf("\n");
            
            /* volume */
            printf("%4d", decode_erp(buf[17], buf[16]));
            printf("\n");
            
            /* tempo */
            printf("%4d", decode_erp(buf[11], buf[10]));
            printf("\n");
            
            /* swing */
            printf("%4d", decode_erp(buf[5],  buf[4]));
            printf("\n");
            
            break;
        }

        case EP1_CMD_READ_IO:
        {
            uint8_t *buf = transfer->buffer + 1;
            size_t   len = transfer->actual_length - 1;
            
            int button_count = sizeof(MaschineKeycodeNames) / sizeof(char *);
            
            for (int i = 0; (i < len * 8) & (i < button_count); i++) {
                int bang = buf[i / 8] & (1 << (i % 8));
                
                if (bang) {
                    printf(" %3d pressed %s\n", i, MaschineKeycodeNames[i]);
                }
            }
            break;
        }
        
        case EP1_CMD_MIDI_READ:
        {
            uint8_t * buf = transfer->buffer + 3;
            int       len = transfer->buffer[2];

            for (int i = 0; i < len; i++) {
                midi_parser_parse(&parser, buf[i]);
            }
            
            break;
        }
        
        case EP1_CMD_MIDI_WRITE:
        case EP1_CMD_DIMM_LEDS:
        case EP1_CMD_AUTO_MSG:
            break;
            
        default:
            printf("unhandled command reply %02x\n", cmd);
            break;
    }
    
    libusb_submit_transfer(transfer);
}

static uint16_t uint16_le_to_cpu(uint16_t le) {
    uint8_t *bytes = (uint8_t *)&le;
    
    return
        (bytes[0] << 0) +
        (bytes[1] << 8);
}

static void ep4_pad_pressure_report_transfer_callback(struct libusb_transfer * transfer) {
    for (int i = 0; i < 16; i++)
    {
        uint16_t *pad_ptr  = (uint16_t *)(transfer->buffer + (i * 2));
        uint16_t  pad      = uint16_le_to_cpu(*pad_ptr);
        uint16_t  pad_id   = (pad & 0xf000) >> 12;
        uint16_t  pressure = (pad & 0x0fff);
        
        pad_id = pad_id;
        pressure = pressure;
        
//        printf("%x:%04x ", pad_id, pressure);
    }
    
//    printf("\n");
    
    libusb_submit_transfer(transfer);
}

static void send_command(libusb_device_handle * maschine, uint8_t * buffer, int len) {
    libusb_bulk_transfer(maschine, 0x01, buffer, len, NULL, 200);
}

static void send_command_get_device_info(libusb_device_handle * maschine) {
    uint8_t command[] = { EP1_CMD_GET_DEVICE_INFO };
    send_command(maschine, command, sizeof(command));
}

static void send_command_set_auto_message(
    libusb_device_handle *maschine,
    uint8_t digital,
    uint8_t analog,
    uint8_t erp
) {
    uint8_t command[] = {
        EP1_CMD_AUTO_MSG,
        digital,
        analog,
        erp,
    };
    
    send_command(maschine, command, sizeof(command));
}


const int MASCHINE_LED_MAX_VAL   = 63;
const int MASCHINE_LED_BANK_SIZE = 32;
const int MASCHINE_LED_CMD_SIZE  = MASCHINE_LED_BANK_SIZE + 2;
const int MASCHINE_LED_BANK0     = MASCHINE_LED_CMD_SIZE * 0;
const int MASCHINE_LED_BANK1     = MASCHINE_LED_CMD_SIZE * 1;

typedef uint8_t MaschineLedState[MASCHINE_LED_CMD_SIZE * 2];

void MaschineLedState_Init(MaschineLedState state) {
    state[MASCHINE_LED_BANK0 + 0] = EP1_CMD_DIMM_LEDS;
    state[MASCHINE_LED_BANK0 + 1] = 0x00;
    
    state[MASCHINE_LED_BANK1 + 0] = EP1_CMD_DIMM_LEDS;
    state[MASCHINE_LED_BANK1 + 1] = 0x1e;
}

void MaschineLedState_SetLed(MaschineLedState state, enum MaschineLeds led, int on) {
    int bank = (led < MASCHINE_LED_BANK_SIZE)
        ? MASCHINE_LED_BANK0
        : MASCHINE_LED_BANK1;
    
    state[bank + 2 + (led % MASCHINE_LED_BANK_SIZE)] = on ? MASCHINE_LED_MAX_VAL : 0;
}

static void send_led_state(
    libusb_device_handle * maschine,
    MaschineLedState state
) {
    send_command(maschine, &state[MASCHINE_LED_BANK0], MASCHINE_LED_CMD_SIZE);
    send_command(maschine, &state[MASCHINE_LED_BANK1], MASCHINE_LED_CMD_SIZE);
}

static void send_command_dimm_leds(
   libusb_device_handle * maschine,
   int bank
) {
    uint8_t command[MASCHINE_LED_BANK_SIZE + 2] = {0};
    
    command[0] = EP1_CMD_DIMM_LEDS;
    command[1] = bank ? 0x1e : 0x00;
    
    static int i = 0;
    command[2 + i] = MASCHINE_LED_MAX_VAL;
    
    if (bank)
        i = (i + 1) % MASCHINE_LED_BANK_SIZE;
    
    send_command(maschine, command, sizeof(command));
}

void receive_ep1_command_responses(libusb_device_handle *maschine) {
    static const size_t length = 64;
    static uint8_t buffer[length] = {0};

    static struct libusb_transfer * transfer = NULL;
    
    if (transfer == NULL)
        transfer = libusb_alloc_transfer(0);

    libusb_fill_bulk_transfer(
        transfer,
        maschine,
        0x81,
        buffer,
        length,
        ep1_command_responses_callback,
        NULL,
        0
    );
    
    int r = libusb_submit_transfer(transfer);
    if (r < 0)
        printf("cannot submit transfer %d\n", r);
}

void receive_ep4_pad_pressure_report(libusb_device_handle *maschine) {
    static const size_t length = 512;
    static uint8_t buffer[length];

    static struct libusb_transfer * transfer = NULL;
    
    if (transfer == NULL)
        transfer = libusb_alloc_transfer(0);

    libusb_fill_bulk_transfer(
        transfer,
        maschine,
        0x84,
        buffer,
        length,
        ep4_pad_pressure_report_transfer_callback,
        NULL,
        0
    );

    int r = libusb_submit_transfer(transfer);
    if (r < 0)
        printf("cannot submit transfer 3 %d\n", r);
}

const int display_width     = 255;
const int display_height    =  64;

const int display_row_size  = display_width * 2 / 3;
const int display_data_size = display_row_size * display_height;

typedef uint8_t MaschineDisplayData[display_data_size];

enum MaschineDisplay {
    MaschineDisplay_Left  = 0 << 1,
    MaschineDisplay_Right = 1 << 1,
};

static void millisecond_sleep(int milli) {
    struct timeval x = {
        .tv_sec = 0,
        .tv_usec = milli * 1000,
    };
    
    libusb_handle_events_timeout(NULL, &x);
}

static void display_init(libusb_device_handle *maschine, enum MaschineDisplay display) {
    uint8_t d = display;

    uint8_t init1[]  = {d, 0x00, 0x01, 0x30};
    uint8_t init2[]  = {d, 0x00, 0x04, 0xCA, 0x04, 0x0F, 0x00};

    uint8_t init3[]  = {d, 0x00, 0x02, 0xBB, 0x00};
    uint8_t init4[]  = {d, 0x00, 0x01, 0xD1};
    uint8_t init5[]  = {d, 0x00, 0x01, 0x94};
    uint8_t init6[]  = {d, 0x00, 0x03, 0x81, 0x1E, 0x02};

    uint8_t init7[]  = {d, 0x00, 0x02, 0x20, 0x08};

    uint8_t init8[]  = {d, 0x00, 0x02, 0x20, 0x0B};

    uint8_t init9[]  = {d, 0x00, 0x01, 0xA6};
    uint8_t init10[] = {d, 0x00, 0x01, 0x31};
    uint8_t init11[] = {d, 0x00, 0x04, 0x32, 0x00, 0x00, 0x05};
    uint8_t init12[] = {d, 0x00, 0x01, 0x34};
    uint8_t init13[] = {d, 0x00, 0x01, 0x30};
    uint8_t init14[] = {d, 0x00, 0x04, 0xBC, 0x00, 0x01, 0x02};
    uint8_t init15[] = {d, 0x00, 0x03, 0x75, 0x00, 0x3F};
    uint8_t init16[] = {d, 0x00, 0x03, 0x15, 0x00, 0x54};
    uint8_t init17[] = {d, 0x00, 0x01, 0x5C};
    uint8_t init18[] = {d, 0x00, 0x01, 0x25};

    uint8_t init19[] = {d, 0x00, 0x01, 0xAF};

    uint8_t init20[] = {d, 0x00, 0x04, 0xBC, 0x02, 0x01, 0x01};
    uint8_t init21[] = {d, 0x00, 0x01, 0xA6};
    uint8_t init22[] = {d, 0x00, 0x03, 0x81, 0x25, 0x02};

    libusb_bulk_transfer(maschine, 0x08, init1,  sizeof(init1),  NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init2,  sizeof(init2),  NULL, 200);
    millisecond_sleep(20);
    
    libusb_bulk_transfer(maschine, 0x08, init3,  sizeof(init3),  NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init4,  sizeof(init4),  NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init5,  sizeof(init5),  NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init6,  sizeof(init6),  NULL, 200);
    millisecond_sleep(20);
    
    libusb_bulk_transfer(maschine, 0x08, init7,  sizeof(init7),  NULL, 200);
    millisecond_sleep(20);
    
    libusb_bulk_transfer(maschine, 0x08, init8,  sizeof(init8),  NULL, 200);
    millisecond_sleep(20);
    
    libusb_bulk_transfer(maschine, 0x08, init9,  sizeof(init9),  NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init10, sizeof(init10), NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init11, sizeof(init11), NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init12, sizeof(init12), NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init13, sizeof(init13), NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init14, sizeof(init14), NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init15, sizeof(init15), NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init16, sizeof(init16), NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init17, sizeof(init17), NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init18, sizeof(init18), NULL, 200);
    millisecond_sleep(20);
    
    libusb_bulk_transfer(maschine, 0x08, init19, sizeof(init19), NULL, 200);
    millisecond_sleep(20);
    
    libusb_bulk_transfer(maschine, 0x08, init20, sizeof(init20), NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init21, sizeof(init21), NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, init22, sizeof(init22), NULL, 200);
}

static void display_send_frame(libusb_device_handle *maschine, MaschineDisplayData data, enum MaschineDisplay display) {
    const int num_chunks     = 22;
    const int data_size      = 502;
    const int last_data_size = 338;
    
    uint8_t d = display;
    
    uint8_t buffer1[]         = { d,     0x00, 0x03, 0x75, 0x00, 0x3f };
    uint8_t buffer2[]         = { d,     0x00, 0x03, 0x15, 0x00, 0x54 };
    uint8_t first_chunk_hdr[] = { d,     0x01, 0xf7, 0x5c };
    uint8_t mid_chunks_hdr[]  = { d + 1, 0x01, 0xf6 };
    uint8_t last_chunk_hdr[]  = { d + 1, 0x01, 0x52 };
    
    uint8_t first_chunk [data_size + sizeof(first_chunk_hdr)] = { 0 };
    uint8_t mid_chunk   [data_size + sizeof(mid_chunks_hdr) ] = { 0 };
    uint8_t last_chunk  [data_size + sizeof(last_chunk_hdr) ] = { 0 };
    
    memcpy(first_chunk, first_chunk_hdr, sizeof(first_chunk_hdr));
    memcpy(mid_chunk,   mid_chunks_hdr,  sizeof(mid_chunks_hdr));
    memcpy(last_chunk,  last_chunk_hdr,  sizeof(last_chunk_hdr));
    
    libusb_bulk_transfer(maschine, 0x08, buffer1, sizeof(buffer1), NULL, 200);
    libusb_bulk_transfer(maschine, 0x08, buffer2, sizeof(buffer2), NULL, 200);

    memcpy(first_chunk + sizeof(first_chunk_hdr), data, data_size);
    libusb_bulk_transfer(maschine, 0x08, first_chunk, sizeof(first_chunk), NULL, 200);
    
    for (int c = 1; c < (num_chunks - 1); c++) {
        memcpy(mid_chunk + sizeof(mid_chunks_hdr), data + (c * data_size), data_size);
        libusb_bulk_transfer(maschine, 0x08, mid_chunk, sizeof(mid_chunk), NULL, 200);
    }
    
    memcpy(last_chunk + sizeof(last_chunk_hdr), data + ((num_chunks - 1) * data_size), last_data_size);
    
    libusb_bulk_transfer(maschine, 0x08, last_chunk, sizeof(last_chunk), NULL, 200);
}

static void display_data_test(MaschineDisplayData display_data) {
    // 0x  f8        1f
    //     1111 1000 0001 1111
    // 0x  07        c0
    //     0000 0111 1100 0000
    
    int xx = 0;
    
    for (int i = 0; i < display_data_size; i++) {
        if ((i % display_row_size) == 0) {
            xx = 0x00;
        }
        
        if ((i % 2) == 0) {
            display_data[i] = (xx << 3) | (xx >> 2);
        }
        else {
            display_data[i] = (xx << 6) | (xx);
        }
        
        xx = (xx + 1) & 0x1f;
    }
}

static void InputPortCallback(
    const MIDIPacketList * pktlist,
    void * refCon,
    void * connRefCon
) {
    uint8_t buffer[512];
    MIDIPacket * packet = (MIDIPacket *)pktlist->packet;

    for (int i = 0; i < pktlist->numPackets; i++) {
        if (packet->length > (sizeof(buffer) - 3))
            continue;
        
        buffer[0] = EP1_CMD_MIDI_WRITE;
        buffer[1] = 0;
        buffer[2] = packet->length;
        memcpy(buffer + 3, packet->data, packet->length);
        
        libusb_bulk_transfer(maschine, 0x01, buffer, packet->length + 3, NULL, 200);
        packet = MIDIPacketNext(packet);
    }
}

static void midi_send(uint8_t *buf, int len) {
    static uint8_t packetData[512];
    
    MIDIPacketList *packetList = (MIDIPacketList *)packetData;
    MIDIPacket *curPacket = NULL;
    
    curPacket = MIDIPacketListInit(packetList);
    curPacket = MIDIPacketListAdd(packetList, sizeof(packetData), curPacket, 0, len, buf);
    
    MIDIReceived(source, packetList);
}

int main(void)
{
    midi_parser_init(&parser, midi_send);
    
    MIDIClientCreate(CFSTR("Simple Maschine MIDI Driver"), NULL, NULL, &client);
    MIDISourceCreate(client, CFSTR("Simple Maschine MIDI In"), &source);
    MIDIDestinationCreate(client, CFSTR("Simple Maschine MIDI Out"), InputPortCallback, NULL, &destination);
    
    /* - */
    
    int r;

    r = libusb_init(NULL);
    if (r < 0)
        printf("cannot init %d\n", r);

    maschine = libusb_open_device_with_vid_pid(
        NULL,
        USB_VID_NATIVEINSTRUMENTS,
        USB_PID_MASCHINECONTROLLER
    );
    
    r = libusb_claim_interface(maschine, 0);
    if (r < 0)
        printf("cannot claim if %d\n", r);
    
    r = libusb_set_interface_alt_setting(maschine, 0, 1);
    if (r < 0)
        printf("cannot set alt setting %d\n", r);
    
    receive_ep1_command_responses(maschine);
    receive_ep4_pad_pressure_report(maschine);

    send_command_get_device_info(maschine);
    send_command_set_auto_message(maschine, 1, 10, 5);

    /* - */
    
    MaschineLedState led_state;
    MaschineLedState_Init(led_state);
    MaschineLedState_SetLed(led_state, MaschineLed_BacklightDisplay, 1);
    
    const int num_pads = 16;
    int show_pads = 0;
    
    MaschineDisplayData display_data;
    display_data_test(display_data);
    
    display_init(maschine, MaschineDisplay_Left);
    display_init(maschine, MaschineDisplay_Right);
    
    display_send_frame(maschine, display_data, MaschineDisplay_Left);
    display_send_frame(maschine, display_data, MaschineDisplay_Right);
    
    while (1) {
        int onoff = !(show_pads / num_pads);
        int pad   =   show_pads % num_pads;
        
        MaschineLedState_SetLed(led_state, MaschineLed_Pad_1 + pad, onoff);
        send_led_state(maschine, led_state);
                
        show_pads++;
        
        if (show_pads > (num_pads * 2))
            show_pads = 0;
        
        millisecond_sleep(80);
    }
    
    return 0;
}
