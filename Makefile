test_pool: test_pool.cpp pool.h
	g++ -Wall -Wextra -pedantic -Werror -Og -o $@ $<
