.PHONY: format
format:
	clang-format-14 -i *.c *.h example/module/ngx_curl_example_module.c
