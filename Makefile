all: demon

demon: demon.cpp main.cpp
	g++ -Wall -Werror -std=c++17 -o demon demon.cpp main.cpp

.PHONY: clean
clean:
	rm -f demon
