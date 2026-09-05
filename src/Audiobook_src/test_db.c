#include <stdio.h>
#include "sqlite3.h"

int main() {
    sqlite3 *db;
    if (sqlite3_open("/usr/data/database/audiobook.db", &db) != SQLITE_OK) {
        printf("Cannot open db\n");
        return 1;
    }
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT root_path FROM books", -1, &stmt, NULL);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("%s\n", sqlite3_column_text(stmt, 0));
        count++;
    }
    printf("Total books: %d\n", count);
    sqlite3_close(db);
    return 0;
}
