CC      = gcc
CFLAGS  = -g -Wall -Ilib/sqlite
SQLFLAGS= -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION
LIBS    = -lm

build/crud: src/main.c build/sqlite3.o
	$(CC) $(CFLAGS) src/main.c build/sqlite3.o -o $@ $(LIBS)

build/sqlite3.o: lib/sqlite/sqlite3.c | build
	$(CC) -c $(SQLFLAGS) $< -o $@

build:
	mkdir -p build

run: build/crud
	./build/crud

clean:
	rm -rf build escuela.db