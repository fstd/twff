.PHONY: all
all:
	make -C src all
	mv src/twff ./
	
.PHONY: clean
clean:
	make -C src clean
	rm -f twff
