
SHADER_DIR = shaders
COMP_DIR = $(SHADER_DIR)/compute
RENDER_DIR = $(SHADER_DIR)/rendering

SPV_DIR = $(SHADER_DIR)/spv

all: compile_shaders
	g++ -c vma.cpp -o vma.o
	gcc -g -c main.c -o main.o
	gcc main.o vma.o -o main -lglfw -lvulkan -lstdc++ -lm

compile_shaders:
	mkdir -p $(SPV_DIR)

	@# Compile compute
	@for file in $$(find $(SHADER_DIR)/* -maxdepth 2 -type f); do \
		if [ -f "$$file" ]; then \
			name=$$(basename $$file); \
			base=$${name%.*}; \
			ext=$${name##*.}; \
			if [ $$ext != "spv" ]; then \
				glslc -fshader-stage=$$ext $$file -o $(SPV_DIR)/$$base-$$ext.spv; \
			fi\
		fi \
	done

clean:
	rm -rf main *.o $(SPV_DIR)

