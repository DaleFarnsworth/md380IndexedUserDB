#include <unistd.h>
#include <stdio.h>
#include <strings.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "hashmap.h"

/*
 * Copyright 2021 Dale Farnsworth
 *
 * This file converts a canonical md380 users database file
 * into an indexed database file containing the same information,
 * but uilizing about half of the space.
 */

#define MAGIC		0x300a01
#define MAGIC_OFFSET	0
#define HEADERSIZE	9
#define INDEXSIZE	6
#define BUFFERSIZE	(15*1024*1024)

#define MAX_FLAGGED_STRING_LENGTH 63

#define NAME_FLAG	(1 << 7)
#define NICKNAME_FLAG	(1 << 6)
#define CITY_FLAG	(1 << 5)
#define STATE_FLAG	(1 << 4)
#define COUNTRY_FLAG	(1 << 3)

#define SHORTCALLSIGN	7

struct user {
	char *id;
	char *callsign;
	char *name;
	char *city;
	char *state;
	char *nickname;
	char *country;
};

char *progname;

struct hashmap_s callsignhash;
struct hashmap_s cityhash;
struct hashmap_s statehash;
struct hashmap_s stringhash;

char *output_buffer;
char *bufferp;
char *buffer_endp;

int node_pool_offset;

void usage() {
	fprintf(stderr, "Usage: %s <linear_db_input_file> <indexed_db_output_file>\n", progname);
	exit(1);
}

void create_node_hashes()
{
	if (hashmap_create(1024, &callsignhash) != 0) {
		fprintf(stderr, "callsign node hash buffer allocation failed\n");
		exit(1);
	}
	if (hashmap_create(1024, &cityhash) != 0) {
		fprintf(stderr, "city node hash buffer allocation failed\n");
		exit(1);
	}
	if (hashmap_create(1024, &statehash) != 0) {
		fprintf(stderr, "state node hash buffer allocation failed\n");
		exit(1);
	}
	if (hashmap_create(1024, &stringhash) != 0) {
		fprintf(stderr, "string node hash buffer allocation failed\n");
		exit(1);
	}
}

char *lookup_node(struct hashmap_s *hash, char *str)
{
	return hashmap_get(hash, str, strlen(str));
}

void hash_node(struct hashmap_s *hash, char *str, char *p)
{
	hashmap_put(hash, str, strlen(str), p);
}

struct user *read_users(char *linear_db_filename, int *nusersp)
{
	FILE *linear_db;
	int i;
	int j;
	char *p;
	ssize_t nread;
	int nusers;
	char *input_buffer;
	int rv;
	struct user *users;
	struct user *up;
	char *user_array[7];
	struct stat statbuf;

	linear_db = fopen(linear_db_filename, "r");
	if (linear_db == NULL) {
		fprintf(stderr, "Can't open %s for reading\n", linear_db_filename);
		usage();
	}

	rv = stat(linear_db_filename, &statbuf);
	if (rv < 0) {
		fprintf(stderr, "stat of %s failed\n", linear_db_filename);
		exit(1);
	}

	input_buffer = malloc(statbuf.st_size);
	if (input_buffer == NULL) {
		fprintf(stderr, "out of memory, input_buffer malloc failed\n");
		exit(1);
	}

	nread = fread(input_buffer, 1, statbuf.st_size + 1, linear_db);
	if (nread != statbuf.st_size) {
		fprintf(stderr, "read of %s failed\n", linear_db_filename);
		exit(1);
	}

	fclose(linear_db);

	if (input_buffer[nread-1] != '\n') {
		input_buffer[nread++] = '\n';
	}

	nusers = 0;
	for (i = 0; i < nread; i++) {
		if (input_buffer[i] == '\n') {
			nusers++;
		}
	}

	users = malloc(nusers * sizeof *users);
	if (users == NULL) {
		fprintf(stderr, "out of memory, users array malloc failed\n");
		exit(1);
	}

	p = input_buffer;
	for (i = 0, up = users; i < nusers; i++, up++) {
		for (j = 0; j < 7; j++) {
			user_array[j] = NULL;
		}

		for (j = 0; j < 7; j++) {
			user_array[j] = p;

			while (*p != ',' && *p != '\n') {
				p++;
			}

			if (*p == '\n') {
				break;
			}

			*p++ = 0;
		}

		if (j < 6 && (i != 0 || j != 0 )) {
			fprintf(stderr, "error: line %d has fewer than 7 fields\n", i+1);
			exit(1);
		}

		if (*p != '\n') {
			fprintf(stderr, "error: line %d has more than 7 fields\n", i+1);
			exit(1);
		}

		*p++ = 0;

		up->id = user_array[0];
		up->callsign = user_array[1];
		up->name = user_array[2];
		up->city = user_array[3];
		up->state = user_array[4];
		up->nickname = user_array[5];
		up->country = user_array[6];
	}

	/*
	 * If file is in .bin format with the byte count as the first line, skip the first entry.
	 * */
	if (users[0].callsign == NULL) {
		users++;
		nusers--;
	}

	*nusersp = nusers;
	return users;
}

int id_int(char *idstr)
{
	int value;
	char *endp;

	value = strtol(idstr, &endp, 10);
	if (*endp != 0) {
		fprintf(stderr, "Bad DMR ID %s, terminating\n", idstr);
		exit(1);
	}

	return value;
}

void set_current_offset(int offset)
{
	bufferp = output_buffer + offset;
}

void set_buffer_length(int offset)
{
	buffer_endp = output_buffer + offset;
}

int get_buffer_length()
{
	return buffer_endp - output_buffer;
}

void put3(int val)
{
	*bufferp++ = (char)(val >> 16);
	*bufferp++ = (char)(val >> 8);
	*bufferp++ = (char)val;
}

void append_flag(int flag)
{
	*buffer_endp++ = (char)flag;
}

void append_offset(int val)
{
	*buffer_endp++ = (char)(val >> 16);
	*buffer_endp++ = (char)(val >> 8);
	*buffer_endp++ = (char)val;
}

void append_2byte_offset(int val)
{
	if (val > 0xffff) {
		fprintf(stderr, "Error: 2-byte offset > 0xffff, exiting!\n");
		exit(1);
	}
	*buffer_endp++ = (char)(val >> 8);
	*buffer_endp++ = (char)val;
}

void append_string_with_flag(char *str, int flag)
{
	int len = strlen(str);

       	if (len > MAX_FLAGGED_STRING_LENGTH)
		len = MAX_FLAGGED_STRING_LENGTH;

	*buffer_endp++ = flag | len;

	strncpy(buffer_endp, str, len);
	buffer_endp += len;
}

void append_string(char *str)
{
	int len = strlen(str);

	*buffer_endp++ = len;

	strncpy(buffer_endp, str, len);
	buffer_endp += len;
}

int append_string_node(char *str)
{
	char *nodep;

	nodep = lookup_node(&stringhash, str);
	if (nodep == NULL) {
		nodep = buffer_endp;
		hash_node(&stringhash, str, nodep);
		append_string(str);
	}

	return nodep - output_buffer;
}

int append_name_node(char *name)
{
	return append_string_node(name);
}

int append_nickname_node(char *nickname)
{
	return append_string_node(nickname);
}

int append_state_node(char *state, char *country);
int append_country_node(char *country);

int append_city_node(char *city, char *state, char *country)
{
	char *nodep;
	char *str = malloc(strlen(city) + 1 + strlen(state) + 1  + strlen(country) + 1);
	int state_offset = 0;
	int country_offset = 0;

	if (str == NULL) {
		fprintf(stderr, "malloc of city+state+country failed\n");
		exit(1);
	}

	strcpy(str, city);
	strcat(str, ",");
	strcat(str, state);
	strcat(str, ",");
	strcat(str, country);

	nodep = lookup_node(&cityhash, str);
	if (nodep == NULL) {
		if (state[0] != 0) {
			state_offset = append_state_node(state, country);
		} else if (country[0] != 0) {
			country_offset = append_country_node(country);
		}

		nodep = buffer_endp; 
		hash_node(&cityhash, str, nodep);

		append_string(city);

		if (state[0] != 0) {
			append_offset(state_offset);
		} else if (country[0] != 0) {
			append_2byte_offset(country_offset);
		}
	}

	return nodep - output_buffer;
}

int append_state_node(char *state, char *country)
{
	char *nodep;
	char *str = malloc(strlen(state) + 1 + strlen(country) + 1);
	int country_offset = 0;

	if (str == NULL) {
		fprintf(stderr, "malloc of state+country failed\n");
		exit(1);
	}
	strcpy(str, state);
	strcat(str, ",");
	strcat(str, country);

	nodep = lookup_node(&statehash, str);
	if (nodep == NULL) {
		if (country[0] != 0) {
			country_offset = append_country_node(country);
		}

		nodep = buffer_endp;
		hash_node(&statehash, str, nodep);

		append_string(state);

		if (country[0] != 0) {
			append_2byte_offset(country_offset);
		}
	}

	return nodep - output_buffer;
}

int append_country_node(char *country)
{
	return append_string_node(country) - node_pool_offset;
}

int append_callsign_node(char *callsign, char *name, char *nickname, char *city, char *state, char *country)
{
	char *nodep;
	char *str = malloc(strlen(callsign) + 1 + strlen(name) + 1 + strlen(nickname) + 1 + strlen(city) + 1 + strlen(state) + 1 + strlen(country) + 1);
	int name_offset = 0;
	int nickname_offset = 0;
	int city_offset = 0;
	int state_offset = 0;
	int country_offset = 0;
	int flag = 0;

	if (str == NULL) {
		fprintf(stderr, "malloc of callsign+name+nickname+city+state+country failed\n");
		exit(1);
	}

	strcpy(str, callsign);
	strcat(str, ",");
	strcat(str, name);
	strcat(str, ",");
	strcat(str, nickname);
	strcat(str, ",");
	strcat(str, city);
	strcat(str, ",");
	strcat(str, state);
	strcat(str, ",");
	strcat(str, country);

	nodep = lookup_node(&callsignhash, str);
	if (nodep == NULL) {
		if (name[0] != 0) {
			flag |= NAME_FLAG;
			name_offset = append_name_node(name);
		}

		if (nickname[0] != 0) {
			flag |= NICKNAME_FLAG;
			nickname_offset = append_nickname_node(nickname);
		}

		if (city[0] != 0) {
			flag |= CITY_FLAG;
			city_offset = append_city_node(city, state, country);
		}

	       	if (state[0] != 0) {
			flag |= STATE_FLAG;
			state_offset = append_state_node(state, country);
		}

	       	if (country[0] != 0) {
			flag |= COUNTRY_FLAG;
			country_offset = append_country_node(country);
		}

		nodep = buffer_endp;
		hash_node(&callsignhash, str, nodep);

		if (callsign[0] == 0 || strlen(callsign) > SHORTCALLSIGN) {
			append_flag(flag);
			flag = 0;
		}

		append_string_with_flag(callsign, flag);

		if (name[0] != 0) {
			append_offset(name_offset);
		}

		if (nickname[0] != 0) {
			append_offset(nickname_offset);
		}

		if (city[0] != 0) {
			append_offset(city_offset);
		} else if (state[0] != 0) {
			append_offset(state_offset);
		} else if (country[0] != 0) {
			append_2byte_offset(country_offset);
		}
	}

	return nodep - output_buffer;
}

int linear_to_indexed(char *linear_db_filename, char *indexed_db_filename)
{
	FILE *indexed_db;
	size_t nwritten;
	int nusers;
	struct user *users;
	struct user *up;
	int i;

	users = read_users(linear_db_filename, &nusers);

	indexed_db = fopen(indexed_db_filename, "w");
	if (indexed_db == NULL) {
		fprintf(stderr, "Can't open %s for writing\n", indexed_db_filename);
		usage();
	}

	create_node_hashes();

	output_buffer = malloc(BUFFERSIZE);
	if (output_buffer == NULL) {
		fprintf(stderr, "out of memory, output_buffer malloc failed\n");
		exit(1);
	}

	set_buffer_length(HEADERSIZE + nusers * INDEXSIZE);
	node_pool_offset = HEADERSIZE + nusers * INDEXSIZE;

	set_current_offset(HEADERSIZE);

	for (i = 0, up = users; i < nusers; i++, up++) {
		if (up->country[0] != 0) {
			append_country_node(up->country);
		}
	}

	for (i = 0, up = users; i < nusers; i++, up++) {
		put3(id_int(up->id));
		put3(append_callsign_node(up->callsign, up->name, up->nickname, up->city, up->state, up->country));
	}

	set_current_offset(MAGIC_OFFSET);

	put3(MAGIC);				/* magic number */
	put3(nusers);				/* number of entries */
	put3(get_buffer_length());		/* size of table */

	nwritten = fwrite(output_buffer, sizeof(char), get_buffer_length(), indexed_db);
	if (nwritten != get_buffer_length()) {
		fprintf(stderr, "write to output file failed (%d != %ld)\n", get_buffer_length(), nwritten);
		exit(1);
	}

	fclose(indexed_db);

	return 0;
}

int main(int argc, char *argv[])
{
	char *linear_db_filename;
	char *indexed_db_filename;
	int rv;

	progname = argv[0];
	argv++;
	argc--;

	if (argc != 2) {
		usage();
	}

	linear_db_filename = argv[0];
	indexed_db_filename = argv[1];

	rv = linear_to_indexed(linear_db_filename, indexed_db_filename);

	return rv;
}
