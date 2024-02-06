PREFIX=../..
GCC_FLAGS=-g 
test01: test01.c test01_scon.so
	gcc ${GCC_FLAGS} -I${PREFIX} ${PREFIX}/*.o test01.c -o test01
test01_scon.so:
	gcc ${GCC_FLAGS} -c test01_scon.c -o test01_scon.o
	gcc ${GCC_FLAGS} --shared test01_scon.o -o test01_scon.so
clean:
	rm -f *.o *.so test01

