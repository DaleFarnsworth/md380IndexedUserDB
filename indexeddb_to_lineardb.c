#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "usersdb.h"

/*
 * Copyright 2021 Dale Farnsworth
 *
 * This file converts a md380 indexed users database file
 * into a canonically formatted md380 users database file.
 */

#define BUFFER_SIZE (16 * 1024 * 1024)

char *progname;

char buffer[BUFFER_SIZE];

char *getdata_offset(char *dest, int offset, int count) {
    memcpy(dest, buffer + offset, count);
    return dest;
}

#define USER_BASE_ADDR     0x100000
#define MAGIC_OFFSET       0
#define USER_COUNT_OFFSET  3
#define INDEX_TABLE_OFFSET 9
#define INDEX_ENTRY_SIZE   6
#define MAGIC_VALUE        (('0' << 16) | ('\n' << 8) | 1)

#define NAME_FLAG          (1 << 7)
#define NICKNAME_FLAG      (1 << 6)
#define CITY_FLAG          (1 << 5)
#define STATE_FLAG         (1 << 4)
#define COUNTRY_FLAG       (1 << 3)

/*
char *getdata_offset(char *dest, int offset, int count) {
    getdata(dest, (const char *)(USER_BASE_ADDR + offset), count);
    return dest;
}
*/

int get3(int offset)
{
    unsigned char buf[3];
    int val;

    getdata_offset((char *)buf, offset, sizeof buf);

    val = buf[0] << 16;
    val |= buf[1] << 8;
    val |= buf[2];

    return val;
}

int get3_incr(int *offsetp)
{
    int result;

    result = get3(*offsetp);
    (*offsetp) += 3;

    return result;
}

int get2_incr(int *offsetp)
{
    unsigned char buf[2];
    int val;

    getdata_offset((char *)buf, *offsetp, sizeof buf);
    (*offsetp) += sizeof buf;

    val = buf[0] << 8;
    val |= buf[1];

    return val;
}

int get1_incr(int *offsetp)
{
    unsigned char buf[1];

    getdata_offset((char *)buf, *offsetp, sizeof buf);
    (*offsetp) += sizeof buf;

    return buf[0];
}

char *getstr_incr(char **destp, int *offsetp, int len, char *dest_end) {
    int maxlen = dest_end - *destp;;
    char *result = dest_end;

    maxlen--;
    if (maxlen > 0) {
        if (maxlen > len) {
            maxlen = len;
        }
        getdata_offset(*destp, *offsetp, maxlen);
        result = *destp;
        *destp += maxlen;
        *(*destp)++ = 0;
    }

    (*offsetp) += len;

    return result;
}

void get_indexed_user(user_t *up, int offset, int user_count)
{
    int dmrid;
    int callsign_offset;
    int name_offset;
    int nickname_offset;
    int city_offset;
    int state_offset;
    int country_offset;
    char *ubufp;
    char *ubuf_end;
    int flag;
    int len;
    int *state_offsetp;
    int *country_offsetp;

    dmrid = get3_incr(&offset);
    callsign_offset = get3(offset);

    ubufp = &up->buffer[0];
    ubuf_end = &up->buffer[sizeof up->buffer - 1];

    up->id = ubufp;
    sprintf(up->id, "%d", dmrid);
    ubufp += strlen(up->id) + 1;

    len = get1_incr(&callsign_offset);
    flag = len & (NAME_FLAG | NICKNAME_FLAG | CITY_FLAG | STATE_FLAG | COUNTRY_FLAG);
    len = len & ~(NAME_FLAG | NICKNAME_FLAG | CITY_FLAG | STATE_FLAG | COUNTRY_FLAG);

    if (len == 0) {
        len = get1_incr(&callsign_offset);
    }

    up->callsign = getstr_incr(&ubufp, &callsign_offset, len, ubuf_end);

    up->name = ubuf_end;
    up->firstname = ubuf_end;
    up->place = ubuf_end;
    up->state = ubuf_end;
    up->country = ubuf_end;

    if (flag & NAME_FLAG) {
        name_offset = get3_incr(&callsign_offset);
        len = get1_incr(&name_offset);
        up->name = getstr_incr(&ubufp, &name_offset, len, ubuf_end);
    }

    if (flag & NICKNAME_FLAG) {
        nickname_offset = get3_incr(&callsign_offset);
        len = get1_incr(&nickname_offset);
        up->firstname = getstr_incr(&ubufp, &nickname_offset, len, ubuf_end);
    }

    state_offsetp = &callsign_offset;
    country_offsetp = &callsign_offset;

    if (flag & CITY_FLAG) {
        city_offset = get3_incr(&callsign_offset);
        len = get1_incr(&city_offset);
        up->place = getstr_incr(&ubufp, &city_offset, len, ubuf_end);

        if (flag & STATE_FLAG) {
            state_offsetp = &city_offset;
        } else if (flag & COUNTRY_FLAG) {
            country_offsetp = &city_offset;
        }
    }

    if (flag & STATE_FLAG) {
        state_offset = get3_incr(state_offsetp);
        len = get1_incr(&state_offset);
        up->state = getstr_incr(&ubufp, &state_offset, len, ubuf_end);

        if (flag & COUNTRY_FLAG) {
            country_offsetp = &state_offset;
        }
    }

    if (flag & COUNTRY_FLAG) {
        country_offset = get2_incr(country_offsetp) + INDEX_TABLE_OFFSET + user_count * INDEX_ENTRY_SIZE;
        len = get1_incr(&country_offset);
        up->country = getstr_incr(&ubufp, &country_offset, len, ubuf_end);
    }

    *ubuf_end = 0;
}

/* returns 0 on failure */
int find_dmr_user_indexed(user_t *up, int dmrid)
{
    int magic;
    int first;
    int last;
    int middle;
    int middleid;
    int user_count;

    magic = get3(MAGIC_OFFSET);
    if (magic != MAGIC_VALUE) {
        return 0;
    } 
    user_count = get3(USER_COUNT_OFFSET);

    first = 0;
    last = user_count - 1;

    /* stifle warnings about middle and middleid being uninitialized */
    middle = 0;
    middleid = 0;

    while (first <= last) {
        middle = (first + last) / 2;
        middleid = get3(middle * INDEX_ENTRY_SIZE + INDEX_TABLE_OFFSET);
        if (middleid < dmrid) {
            first = middle + 1;
        } else if (middleid == dmrid) {
            break;
        } else {
            last = middle - 1;
        }
    }

    if (middleid != dmrid) {
        return 0;
    }

    get_indexed_user(up, middle * INDEX_ENTRY_SIZE + INDEX_TABLE_OFFSET, user_count);

    return 1;
}

void printuser(FILE *f, user_t *up) {
    fprintf(f, "%s,%s,%s,%s,%s,%s,%s\n",
            up->id, up->callsign, up->name, up->place, up->state, up->firstname, up->country);
}

void indexed_to_linear(FILE *outfile)
{
    int i;
    int offset;
    int user_count;
    user_t u;

    user_count = get3(USER_COUNT_OFFSET);

    for (i = 0, offset = INDEX_TABLE_OFFSET; i < user_count; i++, offset += INDEX_ENTRY_SIZE) {
        get_indexed_user(&u, offset, user_count);
        printuser(outfile, &u);
    }
}

void usage()
{
    fprintf(stderr, "Usage: %s <indexed_db_input> <linear_db_output>\n", progname);
    exit(1);
}

int main(int argc, char *argv[])
{
    char *infilename;
    char *outfilename;
    FILE *infile;
    FILE *outfile;
    int nread;
    int magic;

    progname = argv[0];

    if (argc != 3) {
        usage();
    }

    infilename = argv[1];
    infile = fopen(infilename, "r");
    if (infile == NULL) {
        fprintf(stderr, "%s: Can't open for reading\n", infilename);
        usage();
    }

    outfilename = argv[2];
    outfile = fopen(outfilename, "w");
    if (outfile == NULL) {
        fprintf(stderr, "%s: Can't open for writing\n", outfilename);
        usage();
    }

    nread = fread(buffer, 1, sizeof buffer, infile);
    if (nread != sizeof buffer && !feof(infile)) {
        fprintf(stderr, "%s: Failed to read entire file\n", infilename);
        usage();
    }
    fclose(infile);

    magic = get3(MAGIC_OFFSET);
    if (magic != MAGIC_VALUE) {
        fprintf(stderr, "File %s is not in indexed db format (bad magic number)\n", infilename);
    }

    indexed_to_linear(outfile);

    fclose(outfile);

    return 0;
}
