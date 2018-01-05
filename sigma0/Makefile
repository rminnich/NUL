LFLAGS=\
       -T linker.ld \
-Wall\
-Werror\
-O0\
-static\
-mno-red-zone\
-ffreestanding\
-nostdlib \
-fno-builtin\
-march=k8 \
-mcmodel=small

all:
	g++ -nostdinc -I ../include -c *.cc ../service/*.cc
	gcc -nostdinc -I ../include -c ../libc/string.c ../service/*.S
	gcc -o sigma0 $(LFLAGS) *.o
