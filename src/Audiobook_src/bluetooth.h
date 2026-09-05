#ifndef AUDIOBOOK_BLUETOOTH_H
#define AUDIOBOOK_BLUETOOTH_H

typedef struct {
    char mac[18];
    char name[128];
    int paired;
    int connected;
} bt_device_t;

int bt_get_powered(void);
void bt_set_powered(int on);
void bt_scan_start(void);
void bt_scan_stop(void);
int bt_get_devices(bt_device_t *devices, int max_devices);
int bt_connect(const char *mac);
int bt_forget(const char *mac);

#endif
