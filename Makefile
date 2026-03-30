CXX = g++ -fPIC
# NETLIBS= -lnsl

# all: myhttpd use-dlopen hello.so
all: myhttpd

# myhttpd : myhttpd.o
# 	$(CXX) -o $@ $@.o $(NETLIBS)
myhttpd : myhttpd.o
	$(CXX) -o $@ $@.o

# use-dlopen: use-dlopen.o
# 	$(CXX) -o $@ $@.o $(NETLIBS) -ldl
#
# hello.so: hello.o
# 	ld -G -o hello.so hello.o

%.o: %.cc
	@echo 'Building $@ from $<'
	$(CXX) -o $@ -c -I. $<

.PHONY: clean
clean:
	# rm -f *.o use-dlopen hello.so
	rm -f *.o myhttpd

