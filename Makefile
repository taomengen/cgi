cgi:cgi.cpp cgi.h processpool.h
	g++ -o cgi cgi.cpp cgi.h processpool.h -g
clean:
	rm cgi
