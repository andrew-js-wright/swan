PROG=matmul_novec_goto matmul_novec_lapack matmul_novec_atlas

all: $(PROG)

include ../../Makefile.c
include ../../lib/Makefile.use_us

#LDLIBS+=-llapack_atlas
#LDLIBS+=-L$(HOME)/ray/lib -lgoto2

matmul_novec_goto: LDLIBS+=$(HOME)/ray/lib/libgoto2_nehalem-r1.13_T1.a -lm

matmul_novec_goto: matmul_novec.c
	$(CC) $(CFLAGS) matmul_novec.c $(LDFLAGS) $(LDLIBS) -o $@

matmul_novec_lapack: LDLIBS+=-llapack_atlas

matmul_novec_lapack: matmul_novec.c
	$(CC) $(CFLAGS) matmul_novec.c $(LDFLAGS) $(LDLIBS) -o $@

matmul_novec_atlas: LDFLAGS+=-L/opt/atlas/lib
matmul_novec_atlas: LDLIBS+=-llapack -lf77blas -lcblas -latlas -lgfortran

matmul_novec_atlas: matmul_novec.c
	$(CC) $(CFLAGS) matmul_novec.c $(LDFLAGS) $(LDLIBS) -o $@


clean:
	rm -f $(PROG)
