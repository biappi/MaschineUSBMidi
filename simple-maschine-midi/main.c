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
#include <time.h>

#include <libusb/libusb.h>
#include <CoreMIDI/CoreMIDI.h>

#include "midi-state-machine.h"
#include "controls-map.h"

const uint16_t USB_VID_NATIVEINSTRUMENTS  = 0x17cc;
const uint16_t USB_PID_MASCHINECONTROLLER = 0x0808;

const size_t EP1_RESPONSE_TRANSFER_LENGTH =  64;
const size_t EP4_RESPONSE_TRANSFER_LENGTH = 512;

const size_t COMMANDS_QUEUE_SIZE          = 512;

struct Buffer {
    uint8_t *buffer;
    int len;
};

void Buffer_CopyingBytes(struct Buffer *buffer, uint8_t *data, int len) {
    buffer->buffer = malloc(len);
    buffer->len = len;
    memcpy(buffer->buffer, data, len);
}

void Buffer_Free(struct Buffer *buffer) {
    free(buffer->buffer);
    buffer->buffer = NULL;
    buffer->len = 0;
}

struct BufferQueue {
    struct Buffer commands[COMMANDS_QUEUE_SIZE];
    
    int pad0;
    int first;
    int pad1;
    int last;
    int pad2;
};

void BufferQueue_Init(struct BufferQueue *queue) {
    memset(queue, 0, sizeof(struct BufferQueue));
    queue->first = 0;
    queue->last = 0;
}

void BufferQueue_Add(struct BufferQueue *queue, uint8_t *command, int len) {
    int next = queue->last + 1;
    
    if (next >= COMMANDS_QUEUE_SIZE) {
        next -= COMMANDS_QUEUE_SIZE;
        
        if (next >= queue->first) {
            printf("command queue %p overflow\n", queue);
            return;
        }
    }
    
    Buffer_CopyingBytes(&queue->commands[queue->last], command, len);
    queue->last = next;
}

int BufferQueue_IsEmpty(struct BufferQueue *queue) {
    return queue->first == queue->last;
}

struct Buffer* BufferQueue_Peek(struct BufferQueue *queue) {
    if (BufferQueue_IsEmpty(queue)) {
        return NULL;
    }
    
    return &queue->commands[queue->first];
}

void BufferQueue_Remove(struct BufferQueue *queue) {
    if (BufferQueue_IsEmpty(queue)) {
        return;
    }

    Buffer_Free(&(queue->commands[queue->first]));

    queue->first = queue->first + 1;
    
    if (queue->first >= COMMANDS_QUEUE_SIZE) {
        queue->first = 0;
    }
}

const int MASCHINE_LED_MAX_VAL   = 63;
const int MASCHINE_LED_BANK_SIZE = 32;
const int MASCHINE_LED_CMD_SIZE  = MASCHINE_LED_BANK_SIZE + 2;
const int MASCHINE_LED_BANK0     = MASCHINE_LED_CMD_SIZE * 0;
const int MASCHINE_LED_BANK1     = MASCHINE_LED_CMD_SIZE * 1;

typedef uint8_t MaschineLedState[MASCHINE_LED_CMD_SIZE * 2];

struct led_show_state {
    MaschineLedState led_state;
    int num_pads;
    int show_pads;
};

struct Maschine {
    libusb_device_handle *usb_handle;
    
    struct libusb_transfer *ep1_command_transfer;
    struct libusb_transfer *ep1_command_response_transfer;
    struct libusb_transfer *ep4_pad_report_transfer;
    struct libusb_transfer *ep8_display_transfer;
    
    uint8_t ep1_command_response_buffer[EP1_RESPONSE_TRANSFER_LENGTH];
    uint8_t ep4_pad_report_buffer[EP4_RESPONSE_TRANSFER_LENGTH];
    
    struct BufferQueue command_queue;
    int is_transfering_command;
    
    struct BufferQueue display_queue;
    int is_transferring_display;
    
    MIDIClientRef client;
    MIDIEndpointRef source;
    MIDIEndpointRef destination;
    
    midi_parser parser;
    struct led_show_state led_show;
    int display_init_state;
};

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
    struct Maschine *maschine = (struct Maschine *)transfer->user_data;
    
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
                midi_parser_parse(&maschine->parser, buf[i]);
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
    
    int r = libusb_submit_transfer(transfer);
    if (r != LIBUSB_SUCCESS) {
        printf("failed to resubmit ep1 transfer: %d\n", r);
    }
}

static uint16_t uint16_le_to_cpu(uint16_t le) {
    uint8_t *bytes = (uint8_t *)&le;
    
    return
        (bytes[0] << 0) +
        (bytes[1] << 8);
}

static void ep4_pad_pressure_report_transfer_callback(struct libusb_transfer * transfer) {
//    struct Maschine *maschine = (struct Maschine *)transfer->user_data;

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
    
    int r = libusb_submit_transfer(transfer);
    if (r != LIBUSB_SUCCESS) {
        printf("failed to resubmit ep4 transfer: %d\n", r);
    }
}

static void send_command_async_callback(struct libusb_transfer *transfer);

static void send_command_async(struct Maschine * maschine) {
    struct Buffer *buffer = BufferQueue_Peek(&maschine->command_queue);
    
    if (!buffer) {
        maschine->is_transfering_command = 0;
        return;
    }
    
    if (maschine->ep1_command_transfer == NULL)
        maschine->ep1_command_transfer = libusb_alloc_transfer(0);
    
    libusb_fill_bulk_transfer(
        maschine->ep1_command_transfer,
        maschine->usb_handle,
        0x01,
        buffer->buffer,
        buffer->len,
        send_command_async_callback,
        maschine,
        0
    );
    
    int r = libusb_submit_transfer(maschine->ep1_command_transfer);
    if (r != LIBUSB_SUCCESS) {
        printf("failed to submit command: %d\n", r);
        maschine->is_transfering_command = 0;
        return;
    }

    maschine->is_transfering_command = 1;
}

static void send_command_async_callback(struct libusb_transfer *transfer) {
    struct Maschine *maschine = (struct Maschine *)transfer->user_data;
    
    BufferQueue_Remove(&maschine->command_queue);
    send_command_async(maschine);
}


static void send_command(struct Maschine * maschine, uint8_t *buffer, int len) {
    BufferQueue_Add(&maschine->command_queue, buffer, len);
    
    if (!maschine->is_transfering_command) {
        send_command_async(maschine);
    }
}

static void send_command_get_device_info(struct Maschine * maschine) {
    uint8_t command[] = { EP1_CMD_GET_DEVICE_INFO };
    send_command(maschine, command, sizeof(command));
}

static void send_command_set_auto_message(
    struct Maschine *maschine,
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

void MaschineLedState_Init(MaschineLedState state) {
    memset(state, 0, sizeof(MaschineLedState));
    
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
    struct Maschine * maschine,
    MaschineLedState state
) {
    send_command(maschine, &state[MASCHINE_LED_BANK0], MASCHINE_LED_CMD_SIZE);
    send_command(maschine, &state[MASCHINE_LED_BANK1], MASCHINE_LED_CMD_SIZE);
}

/*
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
*/

void receive_ep1_command_responses(struct Maschine *maschine) {
    if (maschine->ep1_command_response_transfer == NULL)
        maschine->ep1_command_response_transfer = libusb_alloc_transfer(0);

    libusb_fill_bulk_transfer(
        maschine->ep1_command_response_transfer,
        maschine->usb_handle,
        0x81,
        maschine->ep1_command_response_buffer,
        sizeof(maschine->ep1_command_response_buffer),
        ep1_command_responses_callback,
        maschine,
        0
    );
    
    int r = libusb_submit_transfer(maschine->ep1_command_response_transfer);
    if (r < 0)
        printf("cannot submit ep1 transfer %d\n", r);
}

void receive_ep4_pad_pressure_report(struct Maschine *maschine) {
    if (maschine->ep4_pad_report_transfer == NULL)
        maschine->ep4_pad_report_transfer = libusb_alloc_transfer(0);

    libusb_fill_bulk_transfer(
        maschine->ep4_pad_report_transfer,
        maschine->usb_handle,
        0x84,
        maschine->ep4_pad_report_buffer,
        sizeof(maschine->ep4_pad_report_buffer),
        ep4_pad_pressure_report_transfer_callback,
        maschine,
        0
    );

    int r = libusb_submit_transfer(maschine->ep4_pad_report_transfer);
    if (r < 0)
        printf("cannot submit ep4 transfer %d\n", r);
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

static void send_display_async_callback(struct libusb_transfer *transfer);

static void send_display_async(struct Maschine * maschine) {
    struct Buffer *buffer = BufferQueue_Peek(&maschine->display_queue);
    
    if (!buffer) {
        maschine->is_transferring_display = 0;
        return;
    }
    
    if (maschine->ep8_display_transfer == NULL)
        maschine->ep8_display_transfer = libusb_alloc_transfer(0);
    
    libusb_fill_bulk_transfer(
        maschine->ep8_display_transfer,
        maschine->usb_handle,
        0x08,
        buffer->buffer,
        buffer->len,
        send_display_async_callback,
        maschine,
        0
    );
    
    int r = libusb_submit_transfer(maschine->ep8_display_transfer);
    if (r != LIBUSB_SUCCESS) {
        printf("failed to submit display tranfser: %d\n", r);
        maschine->is_transferring_display = 0;
        return;
    }
    
    maschine->is_transferring_display = 1;
}

static void send_display_async_callback(struct libusb_transfer *transfer) {
    struct Maschine *maschine = (struct Maschine *)transfer->user_data;

    BufferQueue_Remove(&maschine->display_queue);
    send_display_async(maschine);
}

static void send_display(struct Maschine * maschine, uint8_t *buffer, int len) {
    BufferQueue_Add(&maschine->display_queue, buffer, len);
    
    if (!maschine->is_transferring_display) {
        send_display_async(maschine);
    }
}

static void display_init_1(struct Maschine *maschine, enum MaschineDisplay d) {
    uint8_t init1[]  = {d, 0x00, 0x01, 0x30};
    uint8_t init2[]  = {d, 0x00, 0x04, 0xCA, 0x04, 0x0F, 0x00};
    
    send_display(maschine, init1,  sizeof(init1));
    send_display(maschine, init2,  sizeof(init2));
}

static void display_init_2(struct Maschine *maschine, enum MaschineDisplay d) {
    uint8_t init3[]  = {d, 0x00, 0x02, 0xBB, 0x00};
    uint8_t init4[]  = {d, 0x00, 0x01, 0xD1};
    uint8_t init5[]  = {d, 0x00, 0x01, 0x94};
    uint8_t init6[]  = {d, 0x00, 0x03, 0x81, 0x1E, 0x02};
    
    send_display(maschine, init3,  sizeof(init3));
    send_display(maschine, init4,  sizeof(init4));
    send_display(maschine, init5,  sizeof(init5));
    send_display(maschine, init6,  sizeof(init6));
}

static void display_init_3(struct Maschine *maschine, enum MaschineDisplay d) {
    uint8_t init7[]  = {d, 0x00, 0x02, 0x20, 0x08};
    send_display(maschine, init7,  sizeof(init7));
}

static void display_init_4(struct Maschine *maschine, enum MaschineDisplay d) {
    uint8_t init8[]  = {d, 0x00, 0x02, 0x20, 0x0B};
    send_display(maschine, init8,  sizeof(init8));
}

static void display_init_5(struct Maschine *maschine, enum MaschineDisplay d) {
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
    
    send_display(maschine, init9,  sizeof(init9));
    send_display(maschine, init10, sizeof(init10));
    send_display(maschine, init11, sizeof(init11));
    send_display(maschine, init12, sizeof(init12));
    send_display(maschine, init13, sizeof(init13));
    send_display(maschine, init14, sizeof(init14));
    send_display(maschine, init15, sizeof(init15));
    send_display(maschine, init16, sizeof(init16));
    send_display(maschine, init17, sizeof(init17));
    send_display(maschine, init18, sizeof(init18));
}

static void display_init_6(struct Maschine *maschine, enum MaschineDisplay d) {
    uint8_t init19[] = {d, 0x00, 0x01, 0xAF};
    send_display(maschine, init19, sizeof(init19));
}

static void display_init_7(struct Maschine *maschine, enum MaschineDisplay d) {
    uint8_t init20[] = {d, 0x00, 0x04, 0xBC, 0x02, 0x01, 0x01};
    uint8_t init21[] = {d, 0x00, 0x01, 0xA6};
    uint8_t init22[] = {d, 0x00, 0x03, 0x81, 0x25, 0x02};
    
    send_display(maschine, init20, sizeof(init20));
    send_display(maschine, init21, sizeof(init21));
    send_display(maschine, init22, sizeof(init22));
}

static void display_send_frame(struct Maschine *maschine, MaschineDisplayData data, enum MaschineDisplay display) {
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
    
    send_display(maschine, buffer1, sizeof(buffer1));
    send_display(maschine, buffer2, sizeof(buffer2));

    memcpy(first_chunk + sizeof(first_chunk_hdr), data, data_size);
    send_display(maschine, first_chunk, sizeof(first_chunk));
    
    for (int c = 1; c < (num_chunks - 1); c++) {
        memcpy(mid_chunk + sizeof(mid_chunks_hdr), data + (c * data_size), data_size);
        send_display(maschine, mid_chunk, sizeof(mid_chunk));
    }
    
    memcpy(last_chunk + sizeof(last_chunk_hdr), data + ((num_chunks - 1) * data_size), last_data_size);
    send_display(maschine, last_chunk, sizeof(last_chunk));
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

static void display_send_test_pattern(struct Maschine *maschine, enum MaschineDisplay d) {
    MaschineDisplayData display_data;
    display_data_test(display_data);
    
    display_send_frame(maschine, display_data, MaschineDisplay_Left);
    display_send_frame(maschine, display_data, MaschineDisplay_Right);
}

static void display_init_tick(struct Maschine *maschine) {
    /* here we assume that one "tick" (1/30th of a sec) is enough
     * to transfer a single init step
     */
    
    typedef void (*init_step)(struct Maschine *, enum MaschineDisplay);
    
    static init_step steps[] = {
        display_init_1,
        display_init_2,
        display_init_3,
        display_init_4,
        display_init_5,
        display_init_6,
        display_init_7,
        display_send_test_pattern,
    };
    
    static const int steps_count = sizeof(steps) / sizeof(init_step);
    
    if (maschine->display_init_state >= steps_count) {
        return;
    }
    
    steps[maschine->display_init_state](maschine, MaschineDisplay_Left);
    steps[maschine->display_init_state](maschine, MaschineDisplay_Right);
    maschine->display_init_state++;
}

/* - */

static void InputPortCallback(
    const MIDIPacketList * pktlist,
    void * refCon,
    void * connRefCon
) {
    struct Maschine *maschine = (struct Maschine *)refCon;
    uint8_t buffer[512];
    MIDIPacket * packet = (MIDIPacket *)pktlist->packet;
    
    for (int i = 0; i < pktlist->numPackets; i++) {
        if (packet->length > (sizeof(buffer) - 3))
            continue;
        
        buffer[0] = EP1_CMD_MIDI_WRITE;
        buffer[1] = 0;
        buffer[2] = packet->length;
        memcpy(buffer + 3, packet->data, packet->length);
        
        libusb_bulk_transfer(maschine->usb_handle, 0x01, buffer, packet->length + 3, NULL, 200);
        packet = MIDIPacketNext(packet);
    }
}

static void midi_send(uint8_t *buf, int len, void *user_data) {
    struct Maschine * maschine = (struct Maschine *)user_data;
    
    static uint8_t packetData[512];
    
    MIDIPacketList *packetList = (MIDIPacketList *)packetData;
    MIDIPacket *curPacket = NULL;
    
    curPacket = MIDIPacketListInit(packetList);
    curPacket = MIDIPacketListAdd(packetList, sizeof(packetData), curPacket, 0, len, buf);
    
    MIDIReceived(maschine->source, packetList);
}

/*
static void send_display_test(struct Maschine * maschine) {
    MaschineDisplayData display_data;
    display_data_test(display_data);
    
    display_init(maschine, MaschineDisplay_Left);
    display_init(maschine, MaschineDisplay_Right);
    
    display_send_frame(maschine, display_data, MaschineDisplay_Left);
    display_send_frame(maschine, display_data, MaschineDisplay_Right);
}
*/

static void led_show_init(struct led_show_state * state) {
    state->num_pads = 16;
    state->show_pads = 0;
    
    MaschineLedState_Init(state->led_state);
    MaschineLedState_SetLed(state->led_state, MaschineLed_BacklightDisplay, 1);
}

static void led_show_tick(struct Maschine *maschine) {
    struct led_show_state *state = &maschine->led_show;
    
    int onoff = !(state->show_pads / state->num_pads);
    int pad   =   state->show_pads % state->num_pads;
    
    MaschineLedState_SetLed(state->led_state, MaschineLed_Pad_1 + pad, onoff);
    send_led_state(maschine, state->led_state);
    
    state->show_pads++;
    
    if (state->show_pads > (state->num_pads * 2))
        state->show_pads = 0;
}

int Maschine_Init(
    struct Maschine * maschine,
    libusb_device_handle *device_handle
) {
    int r;
    OSStatus s;

    memset(maschine, 0, sizeof(struct Maschine));
    
    midi_parser_init(&maschine->parser, midi_send, maschine);
    
    s = MIDIClientCreate(
        CFSTR("Simple Maschine MIDI Driver"),
        NULL,
        NULL,
        &maschine->client
    );
    
    if (s != noErr) {
        printf("cannot create midi client: %d\n", s);
//        return -1;
    }
    
    s = MIDISourceCreate(
        maschine->client,
        CFSTR("Simple Maschine MIDI In"),
        &maschine->source
    );
    
    if (s != noErr) {
        printf("cannot create source endpoint: %d\n", s);
//        return -1;
    }

    s = MIDIDestinationCreate(
        maschine->client,
        CFSTR("Simple Maschine MIDI Out"),
        InputPortCallback,
        maschine,
        &maschine->destination
    );

    if (s != noErr) {
        printf("cannot create destination endpoint: %d\n", s);
//        return -1;
    }

    /* - */

    r = libusb_claim_interface(device_handle, 0);
    if (r != LIBUSB_SUCCESS) {
        printf("cannot claim interface %d\n", r);
        return -1;
    }
    
    r = libusb_set_interface_alt_setting(device_handle, 0, 1);
    if (r != LIBUSB_SUCCESS) {
        printf("cannot set alternate interface %d\n", r);
        return -1;
    }

    maschine->usb_handle = device_handle;
    
    BufferQueue_Init(&maschine->command_queue);
    BufferQueue_Init(&maschine->display_queue);
    
    receive_ep1_command_responses(maschine);
    receive_ep4_pad_pressure_report(maschine);
    
    send_command_get_device_info(maschine);
    send_command_set_auto_message(maschine, 1, 10, 5);
    
    /* - */
        
    led_show_init(&maschine->led_show);

    return 0;
}

static void Maschine_disconnect(struct Maschine * maschine) {
    OSStatus s;
    
    s = MIDIEndpointDispose(maschine->source);
    if (s != noErr) {
        printf("cannot dispose source %d\n", s);
    }
    
    MIDIEndpointDispose(maschine->destination);
    if (s != noErr) {
        printf("cannot dispose destination %d\n", s);
    }

    MIDIClientDispose(maschine->client);
    if (s != noErr) {
        printf("cannot dispose client %d\n", s);
    }

    libusb_cancel_transfer(maschine->ep1_command_response_transfer);
    libusb_cancel_transfer(maschine->ep4_pad_report_transfer);
    libusb_cancel_transfer(maschine->ep1_command_transfer);
    
    libusb_close(maschine->usb_handle);
}

static void Maschine_Tick(struct Maschine * maschine) {
    display_init_tick(maschine);
    led_show_tick(maschine);
}

/* - */

static struct Maschine single_maschine;
static int maschine_connected = 0;

static int hotplug_callback(
    struct libusb_context *ctx,
    struct libusb_device *dev,
    libusb_hotplug_event event,
    void *user_data
) {
    int rc;
    
    /*{
        struct libusb_device_descriptor desc;
        libusb_get_device_descriptor(dev, &desc);
    }*/
    
    switch (event) {
        case LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED:
            printf("LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED: ctx: %p dev: %p\n", ctx, dev);
            
            if (maschine_connected) {
                printf("not attaching to device because already connected to one\n");
                break;
            }
            
            libusb_device_handle *handle;
            rc = libusb_open(dev, &handle);
            
            if (LIBUSB_SUCCESS != rc) {
                printf("Could not open USB device\n");
                break;
            }
            
            rc = Maschine_Init(&single_maschine, handle);
            if (rc != 0) {
                printf("cannot connect to the maschine\n");
                break;
            }
            
            maschine_connected = 1;
            
            break;
            
        case LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT:
            printf("LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT: ctx: %p dev: %p\n", ctx, dev);
            
            if (!maschine_connected) {
                printf("not detaching a device because we are not connected\n");
                break;
            }
            
            libusb_device * maschine_device = libusb_get_device(single_maschine.usb_handle);
            
            if (maschine_device != dev) {
                printf("not detaching a device because it's not the one we're attached to\n");
                break;
            }
            
            Maschine_disconnect(&single_maschine);
            maschine_connected = 0;
            
            break;
            
        default:
            printf("Unhandled event %d\n", event);
            break;
    }
    
    return 0;
}

int main(void)
{
    libusb_hotplug_callback_handle callback_handle;

    int r;

    r = libusb_init(NULL);
    if (r < 0)
        printf("cannot init %d\n", r);

    r = libusb_hotplug_register_callback(
        NULL,
        LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
        LIBUSB_HOTPLUG_ENUMERATE,
        USB_VID_NATIVEINSTRUMENTS,
        USB_PID_MASCHINECONTROLLER,
        LIBUSB_HOTPLUG_MATCH_ANY,
        hotplug_callback,
        NULL,
        &callback_handle
    );
    
    if (r != LIBUSB_SUCCESS) {
      printf("Error creating a hotplug callback\n");
      libusb_exit(NULL);
      return EXIT_FAILURE;
    }
    
    
    while (1) {
        if (maschine_connected) {
            Maschine_Tick(&single_maschine);
        }

        libusb_handle_events(NULL);
        usleep(1.0 / 80.0 * 1000000.0);
    }

    return 0;
}
