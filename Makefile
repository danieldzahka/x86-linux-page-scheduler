.PHONY : all mod user clean veryclean veryveryclean lud backprop hotspot kmeans
all: mod user lud backprop hotspot kmeans
mod:
	./scripts/build_mod.sh
user:
	make -C user
rodinia_3.1.tar.bz2:
	wget http://www.cs.virginia.edu/~kw5na/lava/Rodinia/Packages/Current/rodinia_3.1.tar.bz2
rodinia_3.1: rodinia_3.1.tar.bz2
	tar xf rodinia_3.1.tar.bz2
lud: rodinia_3.1
	make -C rodinia_3.1/openmp/lud lud_omp
backprop: rodinia_3.1
	make -C rodinia_3.1/openmp/backprop
hotspot: rodinia_3.1
	make -C rodinia_3.1/openmp/hotspot hotspot
kmeans: rodinia_3.1
	make -C rodinia_3.1/openmp/kmeans
clean:
	./scripts/clean_mod.sh
	make -C user clean
veryclean: clean
	rm -rf rodinia_3.1
veryveryclean: veryclean
	rm rodinia_3.1.tar.bz2
