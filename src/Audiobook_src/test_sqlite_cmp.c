#include <stdio.h>
#include "sqlite3.h"

int main() {
    sqlite3 *db;
    sqlite3_open(":memory:", &db);
    sqlite3_exec(db, "CREATE TABLE books(root_path TEXT);", NULL, NULL, NULL);
    sqlite3_exec(db, "INSERT INTO books VALUES ('/Audiobooks/_Коваленко/_Алан Дин Фостер');", NULL, NULL, NULL);
    sqlite3_exec(db, "INSERT INTO books VALUES ('/Audiobooks/_Коваленко/_Айзек Азимов');", NULL, NULL, NULL);
    sqlite3_exec(db, "INSERT INTO books VALUES ('/Audiobooks/_Коваленко/Александр Прялухин');", NULL, NULL, NULL);
    
    sqlite3_stmt *stmt;
    const char *sql = "SELECT root_path FROM books WHERE "
                      "(root_path >= '/Audiobooks/_Коваленко/' AND "
                      "root_path < '/Audiobooks/_Коваленко0')";
                      
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("MATCH: %s\n", sqlite3_column_text(stmt, 0));
            count++;
        }
        printf("Count: %d\n", count);
    } else {
        printf("Error: %s\n", sqlite3_errmsg(db));
    }
    sqlite3_close(db);
    return 0;
}
