all:
	gcc -o my_htop *.c -Wall -Wextra -Werror -lncurses

clean:
	rm -f my_htop

fclean:
	rm -f my_htop

run: all
	./my_htop

rdebug:
	gcc -o my_htop *.c -Wall -Wextra -Werror -g -lncurses
	gdb ./my_htop

rvalgrind:
	gcc -o my_htop *.c -Wall -Wextra -Werror -g3 -lncurses
	valgrind -s --leak-check=full ./my_htop
