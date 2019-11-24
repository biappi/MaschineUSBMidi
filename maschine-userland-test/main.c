//
//  main.c
//  maschine-userland-test
//
//  Created by Antonio Malara on 24/11/2019.
//  Copyright © 2019 Antonio Malara. All rights reserved.
//

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libusb/libusb.h>

const uint16_t USB_VID_NATIVEINSTRUMENTS  = 0x17cc;
const uint16_t USB_PID_MASCHINECONTROLLER = 0x0808;

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

static const char * keycode_maschine[] = {
    "mute",
    "solo",
    "select",
    "duplicate",
    "navigate",
    "pad",
    "pattern",
    "scene",
    
    "KEY_RESERVED",

    "rec",
    "erase",
    "shift",
    "grid",
    ">",
    "<",
    "restart",

    "E",
    "F",
    "G",
    "H",
    "D",
    "C",
    "B",
    "A",

    "control",
    "browse",
    "<",
    "snap",
    "autowrite",
    ">",
    "sampling",
    "step",

    "soft1",
    "soft2",
    "soft3",
    "soft4",
    "soft5",
    "soft6",
    "soft7",
    "soft8",

    "note repeat",
    "play",
};

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
            
            int button_count = sizeof(keycode_maschine) / sizeof(char *);
            
            for (int i = 0; (i < len * 8) & (i < button_count); i++) {
                int bang = buf[i / 8] & (1 << (i % 8));
                
                if (bang) {
                    printf(" %3d pressed %s\n", i, keycode_maschine[i]);
                }
            }
            break;
        }
        
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

static void send_command_dimm_leds(
   libusb_device_handle * maschine,
   int bank
) {
    const int MASCHINE_BANK_SIZE = 32;
    const int MAX_VAL = 63;
    
    uint8_t command[MASCHINE_BANK_SIZE + 2] = {0};
    
    command[0] = EP1_CMD_DIMM_LEDS;
    command[1] = bank ? 0x1e : 0x00;
    
    static int i = 0;
    command[2 + i] = MAX_VAL;
    
    if (bank)
        i = (i + 1) % MASCHINE_BANK_SIZE;
    
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

int main(void)
{
    int r;

    r = libusb_init(NULL);
    if (r < 0)
        printf("cannot init %d\n", r);

    libusb_device_handle *
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

    while (1) {
        send_command_dimm_leds(maschine, 0);
        send_command_dimm_leds(maschine, 1);
        
        struct timeval x = {
            .tv_sec = 0,
            .tv_usec = 80000,
        };
        
        libusb_handle_events_timeout(NULL, &x);
    }
    
    return 0;
}
