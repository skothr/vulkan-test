CXX      = g++
CXXFLAGS = -std=c++17 -O2
LDFLAGS  = -lvulkan -lglfw -ldl -lpthread

TARGET   = vulkan-cube
SRCS     = src/main.cpp

SHADER_DIR = shaders
SPV_DIR    = shaders/compiled
VERT_SPV   = $(SPV_DIR)/shader.vert.spv
FRAG_SPV   = $(SPV_DIR)/shader.frag.spv

.PHONY: all run clean shaders

all: shaders $(TARGET)

shaders: $(SPV_DIR) $(VERT_SPV) $(FRAG_SPV)

$(SPV_DIR):
	mkdir -p $(SPV_DIR)

$(VERT_SPV): $(SHADER_DIR)/shader.vert
	glslangValidator -V $< -o $@

$(FRAG_SPV): $(SHADER_DIR)/shader.frag
	glslangValidator -V $< -o $@

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET) $(VERT_SPV) $(FRAG_SPV)
