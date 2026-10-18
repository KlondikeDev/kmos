// This file has been generated in whole or in part by a Large Language Model
#pragma once
// Kunix device ABI, also usable through the Linux personality's ioctl.
// Submit returns a positive job ID. RESULT takes that ID and is nondestructive.
#define KWIFI_SCAN       0x4b5701u
#define KWIFI_JOIN       0x4b5702u
#define KWIFI_DISCONNECT 0x4b5703u
#define KWIFI_RESULT     0x4b5704u
#define KWIFI_MAX_RESULTS 32u
struct kwifi_join {
    unsigned int ssid_length, passphrase_length;
    unsigned char ssid[32];
    char passphrase[64]; // zero terminated; 8..63 bytes, or empty for open
};
struct kwifi_network {
    unsigned char bssid[6], ssid[32], ssid_length, channel;
    short signal_dbm;
    unsigned char privacy, reserved;
};
struct kwifi_result {
    unsigned int job, complete;
    int error;
    unsigned int count;
    struct kwifi_network networks[KWIFI_MAX_RESULTS];
};
