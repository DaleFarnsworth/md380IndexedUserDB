lineardb_to_indexeddb:	lineardb_to_indexeddb.c
	cc -O -Wall lineardb_to_indexeddb.c -o lineardb_to_indexeddb

indexeddb_to_lineardb:	indexeddb_to_lineardb.c
	cc -O -Wall indexeddb_to_lineardb.c -o indexeddb_to_lineardb
