

randnorm: randnorm.o
	gcc -o $@ -L/usr/local/lib -I.. -I../../include $^ -lgsl -lgslcblas -lm

randnorm.o: randnorm.c
	gcc -Wall -I/usr/local/include -I.. -I../../include -c $^

.PHONY:clean
clean:
	rm *.o
	rm randnorm
