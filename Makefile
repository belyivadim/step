.PHONY: clean

SRC = main.c
FLAGS = -g -Wall -Wextra -pedantic -std=c11

step: $(SRC)
	$(CC) $(FLAGS) -o step $(SRC)

clean:
	rm -f step
