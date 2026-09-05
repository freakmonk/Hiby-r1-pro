#include "bluetooth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int run_cmd(const char *cmd, char *out, int out_size) {
    FILE *f = popen(cmd, "r");
    if (!f) return 0;
    int len = 0;
    if (out && out_size > 0) {
        while (fgets(out + len, out_size - len, f)) {
            len += strlen(out + len);
            if (len >= out_size - 1) break;
        }
    }
    int rc = pclose(f);
    return rc == 0;
}

int bt_get_powered(void) {
    char out[1024] = {0};
    run_cmd("bluetoothctl show", out, sizeof(out));
    return strstr(out, "Powered: yes") != NULL;
}

void bt_set_powered(int on) {
    if (on) {
        system("/usr/bin/bt_enable");
    } else {
        system("/usr/bin/bt_disable");
    }
}

void bt_scan_start(void) {
    system("bluetoothctl scan on >/dev/null 2>&1 &");
}

void bt_scan_stop(void) {
    system("bluetoothctl scan off >/dev/null 2>&1 &");
}

int bt_get_devices(bt_device_t *devices, int max_devices) {
    char out[8192] = {0};
    /* First get paired devices */
    run_cmd("bluetoothctl paired-devices", out, sizeof(out));
    int count = 0;
    char *line = strtok(out, "\n");
    while (line && count < max_devices) {
        if (strncmp(line, "Device ", 7) == 0) {
            char mac[18] = {0};
            strncpy(mac, line + 7, 17);
            strncpy(devices[count].mac, mac, 18);
            strncpy(devices[count].name, line + 25, sizeof(devices[count].name)-1);
            devices[count].paired = 1;
            /* Check if connected */
            char info_cmd[256];
            snprintf(info_cmd, sizeof(info_cmd), "bluetoothctl info %s", mac);
            char info_out[2048] = {0};
            run_cmd(info_cmd, info_out, sizeof(info_out));
            devices[count].connected = (strstr(info_out, "Connected: yes") != NULL);
            count++;
        }
        line = strtok(NULL, "\n");
    }
    
    /* Then get all devices from discovery */
    char out2[8192] = {0};
    run_cmd("bluetoothctl devices", out2, sizeof(out2));
    line = strtok(out2, "\n");
    while (line && count < max_devices) {
        if (strncmp(line, "Device ", 7) == 0) {
            char mac[18] = {0};
            strncpy(mac, line + 7, 17);
            /* Check if already added */
            int found = 0;
            for (int i=0; i<count; i++) {
                if (strcmp(devices[i].mac, mac) == 0) { found = 1; break; }
            }
            if (!found) {
                strncpy(devices[count].mac, mac, 18);
                strncpy(devices[count].name, line + 25, sizeof(devices[count].name)-1);
                devices[count].paired = 0;
                devices[count].connected = 0;
                count++;
            }
        }
        line = strtok(NULL, "\n");
    }
    return count;
}

int bt_connect(const char *mac) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "bluetoothctl pair %s", mac);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "bluetoothctl trust %s", mac);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "bluetoothctl connect %s", mac);
    char out[1024] = {0};
    run_cmd(cmd, out, sizeof(out));
    return strstr(out, "Failed") == NULL;
}

int bt_forget(const char *mac) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "bluetoothctl remove %s", mac);
    char out[1024] = {0};
    run_cmd(cmd, out, sizeof(out));
    return strstr(out, "Failed") == NULL;
}
