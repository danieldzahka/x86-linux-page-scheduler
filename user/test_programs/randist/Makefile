
.PHONY:all
all: randnorm laplace

bin/randnorm: randnorm.o | bin
	gcc -o $@ -L/usr/local/lib -I.. -I../../include $^ -lgsl -lgslcblas -lm

randnorm.o: randnorm.c
	gcc -Wall -I/usr/local/include -I.. -I../../include -c $^

bin/laplace: laplace.o | bin
	gcc -o $@ -L/usr/local/lib -I.. -I../../include $^ -lgsl -lgslcblas -lm

laplace.o: laplace.c
	gcc -Wall -I/usr/local/include -I.. -I../../include -c $^

bin:
	mkdir bin

.PHONY:clean
clean:
	rm *.o
	rm randnorm
	rm laplace
