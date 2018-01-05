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
-fno-stack-protector\
-march=k8 \
-mcmodel=small

CFLAGS=\
       -fno-stack-protector\
	-fno-threadsafe-statics \
       -nostdinc\
       -I ../include

OFILES= \
capalloc.o \
cpp-support.o \
help.o \
hostsink.o \
kbd2serial.o \
logging.o \
scripting.o \
serial2kbd.o \
service_config.o \
service_fs.o \
service_per_cpu_timer.o \
service_tracebuffer.o \
simplemalloc.o \
startup.o \
string.o \
vars.o \
vprintf.o \

all:
	rm -f sigma0.a
	g++ $(CFLAGS) -c *.cc ../service/*.cc ../runtime/*.cc
	gcc $(CFLAGS) -c ../libc/mem.c ../service/*.S 
	ar -qc sigma0.a $(OFILES)
	gcc -o sigma0 $(LFLAGS) sigma0.o sigma0.a
