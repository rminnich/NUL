CFLAGS=\
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
	gcc $(CFLAGS) test.s
