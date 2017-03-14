wSender: wSender.o
	g++ wSender.o -o wSender

wSender.o: wSender.cpp
	g++ $< -I -ldl -std=c++11 -c -o $@ -g -fno-inline

clean:
	-rm -rf *.o
	-rm wSender

