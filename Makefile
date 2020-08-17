# lab1b Makefile
# NAME: Arnold Pfahnl
# EMAIL: ajpfahnl@gmail.com
# ID: 305176399

all: lab1b-client lab1b-server

common = common.c common.h
flags = -Wall -Wextra -lz

lab1b-client: lab1b-client.c $(common)
	gcc -o lab1b-client lab1b-client.c common.c $(flags)

lab1b-server: lab1b-server.c $(common)
	gcc -o lab1b-server common.c lab1b-server.c $(flags)

.PHONY: clean dist
clean:
	rm lab1b-client lab1b-server lab1b-305176399.tar.gz

tar_files = lab1b-client.c lab1b-server.c Makefile README $(common)
dist: $(tar_files)
	tar -z -c -f lab1b-305176399.tar.gz $(tar_files)
