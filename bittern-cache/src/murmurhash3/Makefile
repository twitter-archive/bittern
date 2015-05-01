.PHONY: all
all: murmurhash3_test

murmurhash3_test: murmurhash3.c murmurhash3.h murmurhash3_test.c
	gcc -o murmurhash3_test -O2 murmurhash3_test.c murmurhash3.c

.PHONY: clean distclean
clean distclean:
	rm -f murmurhash3_test *.o .*.cmd
