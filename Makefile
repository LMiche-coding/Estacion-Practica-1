CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -D_DEFAULT_SOURCE -Ilib/sqlite 
LIBS    = -lcurl -lm 
OBJS    = build/main.o build/red.o build/datos.o build/sensor.o build/sqlite3.o

all: estacion 

estacion: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

build/sqlite3.o: lib/sqlite/sqlite3.c
	@mkdir -p build
	$(CC) -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -c $< -o $@

build/%.o: src/%.c
	@mkdir -p build 
	$(CC) -c $(CFLAGS) -c $< -o $@

run: estacion
	./estacion config

clean:
	rm -rf build estacion estacion.exe