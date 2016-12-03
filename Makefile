all:
	gcc proxee.c -o proxee
	strip proxee

clean:
	rm -f proxee proxee.log *~
