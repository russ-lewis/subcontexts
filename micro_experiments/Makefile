GCC_FLAGS = -g
all: exp1.c exp2.c exp3_service.c exp3_process.c exp4_main.c exp4_lib.c
	gcc ${GCC_FLAGS} exp1.c -o exp1
	gcc ${GCC_FLAGS} exp2.c -o exp2
	gcc ${GCC_FLAGS} exp3_service.c -o exp3_service
	gcc ${GCC_FLAGS} exp3_process.c -o exp3_process
	gcc ${GCC_FLAGS} -c exp4_lib.c -o exp4_lib.o
	gcc -shared -o exp4_lib.so exp4_lib.o
	gcc ${GCC_FLAGS} exp4_main.c -o exp4_main
exp1: exp1.c
	gcc ${GCC_FLAGS} exp1.c -o exp1
exp2: exp2.c
	gcc ${GCC_FLAGS} exp2.c -o exp2
exp3_service: exp3_service.c
	gcc ${GCC_FLAGS} exp3_service.c -o exp3_service
exp3_process: exp3_process.c
	gcc ${GCC_FLAGS} exp3_process.c -o exp3_process
exp4_lib.so: exp4_lib.c
	gcc ${GCC_FLAGS} -c exp4_lib.c -o exp4_lib.o
	gcc -shared -o exp4_lib.so exp4_lib.o
exp4_main: exp4_main.c
	gcc ${GCC_FLAGS} exp4_main.c -o exp4_main
clean:
	rm -f *.o *.so exp1 exp2 exp3_service exp3_process exp4_main
