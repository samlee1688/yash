yash: yash.o
	gcc -o yash yash.o -lreadline

yash.o: yash.c
	gcc -c yash.c

clean:
	rm -f yash *.o