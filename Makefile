all: myldd

myldd: my_ldd.cpp my_ldd.h
	g++ -g -Wall $< -o $@ -lelf


clean:
	rm -f myldd
