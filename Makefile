GaragePythons:	GaragePythons.c
	gcc -c -g -std=c99 GaragePythons.c; gcc -g -std=c99 -o GaragePythons GaragePythons.o -ljpeg -lcrypto -lm
clean:
	rm -f GaragePythons.o GaragePythons stego*
