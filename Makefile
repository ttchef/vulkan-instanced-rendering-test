
all:
	glslc -fshader-stage=vert default.vert -o default_vert.spv
	glslc -fshader-stage=frag default.frag -o default_frag.spv
	glslc -fshader-stage=comp default.comp -o default_comp.spv
	g++ -c vma.cpp -o vma.o
	gcc -g -c main.c -o main.o
	gcc main.o vma.o -o main -lglfw -lvulkan -lstdc++ -lm

clean:
	rm -rf main *.o *.spv
