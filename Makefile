all: devreader

devreader: devreader.c
	gcc -Wall --static -o $@ $<

clean: devreader
	rm -rf $<
