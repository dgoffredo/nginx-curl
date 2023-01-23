test_pool : test_pool.cpp pool.h pool.cpp
	g++ -Wall -Wextra -pedantic -Werror -Og -o $@ test_pool.cpp pool.cpp

.PHONY: format
format:
	clang-format-14 -i *.cpp *.c *.h example/module/ngx_curl_example_module.c
