
all:
	glslc -fshader-stage=vert default.vert -o default_vert.spv
	glslc -fshader-stage=frag default.frag -o default_frag.spv
	glslc -fshader-stage=comp cell_hash.comp -o cell_hash.spv
	glslc -fshader-stage=comp radix_sort.comp -o radix_sort.spv
	glslc -fshader-stage=comp start_indicies.comp -o start_indicies.spv
	glslc -fshader-stage=comp particle_update.comp -o particle_update.spv
	g++ -c vma.cpp -o vma.o
	gcc -g -c main.c -o main.o
	gcc main.o vma.o -o main -lglfw -lvulkan -lstdc++ -lm

clean:
	rm -rf main *.o *.spv

