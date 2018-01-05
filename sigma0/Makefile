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
	g++ -nostdinc -ffreestanding -I ../include -c *.cc ../libc/string.c
	gcc -o sigma0 $(LFLAGS) *.o
