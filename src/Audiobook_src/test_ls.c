#include <stdio.h>
#include <dirent.h>

int main() {
    DIR *d = opendir("/usr/data/mnt/sd_0/Audiobooks/_Коваленко");
    if (!d) {
        printf("Failed to open _Коваленко\n");
        return 1;
    }
    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        printf("FOUND: %s\n", ent->d_name);
        count++;
    }
    closedir(d);
    printf("Total items: %d\n", count);
    return 0;
}
