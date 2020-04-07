//
//  controls-map.h
//  simple-maschine-midi
//
//  Created by Antonio Malara on 01/12/2019.
//  Copyright © 2019 Antonio Malara. All rights reserved.
//

#ifndef controls_map_h
#define controls_map_h

enum MaschineKeycodes {
    MaschineKeycode_Mute,
    MaschineKeycode_Solo,
    MaschineKeycode_Select,
    MaschineKeycode_Duplicate,
    MaschineKeycode_Navigate,
    MaschineKeycode_Pad,
    MaschineKeycode_Pattern,
    MaschineKeycode_Scene,
    
    MaschineKeycode_Unused,

    MaschineKeycode_Rec,
    MaschineKeycode_Erase,
    MaschineKeycode_Shift,
    MaschineKeycode_Grid,
    MaschineKeycode_TransportRight,
    MaschineKeycode_TransportLeft,
    MaschineKeycode_Restart,

    MaschineKeycode_GroupE,
    MaschineKeycode_GroupF,
    MaschineKeycode_GroupG,
    MaschineKeycode_GroupH,
    MaschineKeycode_GroupD,
    MaschineKeycode_GroupC,
    MaschineKeycode_GroupB,
    MaschineKeycode_GroupA,

    MaschineKeycode_Control,
    MaschineKeycode_Browse,
    MaschineKeycode_Left,
    MaschineKeycode_Snap,
    MaschineKeycode_Autowrite,
    MaschineKeycode_Right,
    MaschineKeycode_Sampling,
    MaschineKeycode_Step,

    MaschineKeycode_Soft1,
    MaschineKeycode_Soft2,
    MaschineKeycode_Soft3,
    MaschineKeycode_Soft4,
    MaschineKeycode_Soft5,
    MaschineKeycode_Soft6,
    MaschineKeycode_Soft7,
    MaschineKeycode_Soft8,

    MaschineKeycode_NoteRepeat,
    MaschineKeycode_Play,
};

enum MaschineLeds {
    MaschineLed_Pad_1,
    MaschineLed_Pad_2,
    MaschineLed_Pad_3,
    MaschineLed_Pad_4,
    MaschineLed_Pad_5,
    MaschineLed_Pad_6,
    MaschineLed_Pad_7,
    MaschineLed_Pad_8,
    MaschineLed_Pad_9,
    MaschineLed_Pad_10,
    MaschineLed_Pad_11,
    MaschineLed_Pad_12,
    MaschineLed_Pad_13,
    MaschineLed_Pad_14,
    MaschineLed_Pad_15,
    MaschineLed_Pad_16,

    MaschineLed_Mute,
    MaschineLed_Solo,
    MaschineLed_Select,
    MaschineLed_Duplicate,
    MaschineLed_Navigate,
    MaschineLed_PadMode,
    MaschineLed_Pattern,
    MaschineLed_Scene,

    MaschineLed_Shift,
    MaschineLed_Erase,
    MaschineLed_Grid,
    MaschineLed_TransportRight,
    MaschineLed_Rec,
    MaschineLed_Play,
    
    MaschineLed_Unused1,
    MaschineLed_Unused2,
    
    MaschineLed_TransportLeft,
    MaschineLed_Restart,

    MaschineLed_GroupA,
    MaschineLed_GroupB,
    MaschineLed_GroupC,
    MaschineLed_GroupD,
    MaschineLed_GroupE,
    MaschineLed_GroupF,
    MaschineLed_GroupG,
    MaschineLed_GroupH,

    MaschineLed_AutoWrite,
    MaschineLed_Snap,
    MaschineLed_Right,
    MaschineLed_Left,
    MaschineLed_Sampling,
    MaschineLed_Browse,
    MaschineLed_Step,
    MaschineLed_Control,

    MaschineLed_Soft1,
    MaschineLed_Soft2,
    MaschineLed_Soft3,
    MaschineLed_Soft4,
    MaschineLed_Soft5,
    MaschineLed_Soft6,
    MaschineLed_Soft7,
    MaschineLed_Soft8,

    MaschineLed_NoteRepeat,

    MaschineLed_BacklightDisplay,
};

static const char * MaschineKeycodeNames[] = {
    "mute",
    "solo",
    "select",
    "duplicate",
    "navigate",
    "pad",
    "pattern",
    "scene",
    
    "unused",

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

#endif /* controls_map_h */
