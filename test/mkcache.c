#include <stdio.h>
#include <stdlib.h>

#include "timetable.h"

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: mkcache <esolat.json> <zone> <year> <out.bin>\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    static char json[1 << 20];
    size_t len = fread(json, 1, sizeof(json), f);
    fclose(f);
    static year_table_t t;
    if (esolat_parse(json, len, argv[2], (int16_t)atoi(argv[3]), &t) != PARSE_OK) {
        fprintf(stderr, "no data for %s %s in %s\n", argv[2], argv[3], argv[1]);
        return 1;
    }
    static uint8_t blob[YEAR_TABLE_BLOB_MAX];
    size_t n = year_table_serialize(&t, blob, sizeof(blob));
    FILE *o = fopen(argv[4], "wb");
    if (!o || fwrite(blob, 1, n, o) != n || fclose(o) != 0) {
        perror(argv[4]);
        return 1;
    }
    printf("%s: %s %s, %d days\n", argv[4], argv[2], argv[3], year_table_day_count(&t));
    return 0;
}
