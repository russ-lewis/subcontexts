GCC_FLAGS = -g
all:
	${MAKE} exp1
	${MAKE} exp2
	${MAKE} exp3_service
	${MAKE} exp3_process
	${MAKE} exp4
	${MAKE} exp5
exp1: exp1.c
	gcc ${GCC_FLAGS} exp1.c -o exp1
exp2: exp2.c
	gcc ${GCC_FLAGS} exp2.c -o exp2
exp3_service: exp3_service.c
	gcc ${GCC_FLAGS} exp3_service.c -o exp3_service
exp3_process: exp3_process.c
	gcc ${GCC_FLAGS} exp3_process.c -o exp3_process
exp4: exp4.c exp4_lib.so map.o
	gcc ${GCC_FLAGS} exp4.c ../src/mem/map.o -o exp4
exp4_lib.so: exp4_lib.c
	gcc ${GCC_FLAGS} -shared  -c exp4_lib.c -o exp4_lib.o
	gcc -shared exp4_lib.o -o exp4_lib.so
exp5: exp5_shim.so
	gcc ${GCC_FLAGS} exp5.c -o exp5
exp5_shim.so: exp5_shim.c
	gcc ${GCC_FLAGS} -c exp5_shim.c -o exp5_shim.o
	gcc -shared exp5_shim.o -o exp5_shim.so
map.o:
	${MAKE} -C ../src/mem
clean:
	rm -f *.o *.so exp1 exp2 exp3_service exp3_process exp4 exp5
	${MAKE} clean -C ../src/mem
