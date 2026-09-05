#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>

int main() {
    struct stat st;
    if (stat("/usr/data/mnt/sd_0/Audiobooks/_Коваленко", &st) == 0) {
        printf("Stat OK\n");
    } else {
        printf("Stat failed\n");
    }
    return 0;
}
