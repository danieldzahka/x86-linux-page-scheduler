
.PHONY:all
all: randnorm laplace

randnorm: randnorm.o
	gcc -o $@ -L/usr/local/lib -I.. -I../../include $^ -lgsl -lgslcblas -lm

randnorm.o: randnorm.c
	gcc -Wall -I/usr/local/include -I.. -I../../include -c $^

laplace: laplace.o
	gcc -o $@ -L/usr/local/lib -I.. -I../../include $^ -lgsl -lgslcblas -lm

laplace.o: laplace.c
	gcc -Wall -I/usr/local/include -I.. -I../../include -c $^


.PHONY:clean
clean:
	rm *.o
	rm randnorm
	rm laplace
