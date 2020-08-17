all: twoface-client twoface-server

common = common.c common.h
flags = -Wall -Wextra -lz

twoface-client: twoface-client.c $(common)
	gcc -o twoface-client twoface-client.c common.c $(flags)

twoface-server: twoface-server.c $(common)
	gcc -o twoface-server common.c twoface-server.c $(flags)

.PHONY: clean dist
clean:
	rm twoface-client twoface-server twoface.tar.gz

tar_files = twoface-client.c twoface-server.c Makefile README $(common)
dist: $(tar_files)
	tar -z -c -f twoface.tar.gz $(tar_files)
