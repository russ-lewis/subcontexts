GCC_FLAGS=-g -I.
TESTS=$(wildcard tests/*)
TESTSCLEAN = $(addsuffix .clean,${TESTS})

scon.o: scon.h scon.c map.o
	gcc ${GCC_FLAGS} -c scon.c -o scon.o
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
        
.PHONY: map.o tests ${TESTS} clean ${TESTSCLEAN}
