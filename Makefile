.PHONY: clean

SRC = main.c
FLAGS = -g -Wall -Wextra -pedantic -std=c11

lifo: $(SRC)
	$(CC) $(FLAGS) -o lifo $(SRC)

clean:
	rm -f lifo
