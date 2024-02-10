GCC_FLAGS=-g -I.
TESTS=$(wildcard tests/*)
TESTSCLEAN = $(addsuffix .clean,${TESTS})

scon.o: scon.h scon.c _scbtable.o map.o
	gcc ${GCC_FLAGS} -c scon.c -o scon.o
_scbtable.o:
	gcc ${GCC_FLAGS} -c _scbtable.c -o _scbtable.o
map.o: 
	${MAKE} -C mem
	cp mem/map.o .
tests: scon.o ${TESTS}
${TESTS}:
	${MAKE} -C $@ 
clean: ${TESTSCLEAN}
	rm -f *.o
	${MAKE} clean -C mem
$(TESTSCLEAN): %.clean:
	${MAKE} clean -C $*
        
.PHONY: scon.o _scbtable.o map.o tests ${TESTS} clean ${TESTSCLEAN}
