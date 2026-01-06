
all:
	glslc -fshader-stage=vert default.vert -o default_vert.spv
	glslc -fshader-stage=frag default.frag -o default_frag.spv
	gcc main.c -o main -lglfw -lvulkan
