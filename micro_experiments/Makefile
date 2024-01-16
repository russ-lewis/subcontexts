GCC_FLAGS = -g
all: exp1.c exp2.c exp3_service.c exp3_process.c
	gcc ${GCC_FLAGS} exp1.c -o exp1
	gcc ${GCC_FLAGS} exp2.c -o exp2
	gcc ${GCC_FLAGS} exp3_service.c -o exp3_service
	gcc ${GCC_FLAGS} exp3_process.c -o exp3_process
exp1: exp1.c
	gcc ${GCC_FLAGS} exp1.c -o exp1
exp2: exp2.c
	gcc ${GCC_FLAGS} exp2.c -o exp2
exp3_service: exp3_service.c
	gcc ${GCC_FLAGS} exp3_service.c -o exp3_service
exp3_process: exp3_process.c
	gcc ${GCC_FLAGS} exp3_process.c -o exp3_process
clean:
	rm -f *.o exp1 exp2 exp3_service exp3_process
