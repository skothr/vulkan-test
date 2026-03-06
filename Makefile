CXX      = g++
CXXFLAGS = -std=c++17 -O2
LDFLAGS  = -lvulkan -lglfw -ldl -lpthread -limgui -lstb

TARGET   = vulkan-cube
SRCS     = src/main.cpp src/Application.cpp src/ControlPanel.cpp \
           lib/imgui-backends/imgui_impl_glfw.cpp \
           lib/imgui-backends/imgui_impl_vulkan.cpp
INCLUDES = -Iinclude -I/usr/include/imgui -I/usr/include/imgui/backends

SHADER_DIR = shaders
SPV_DIR    = shaders/compiled
RGEN_SPV   = $(SPV_DIR)/shader.rgen.spv
RMISS_SPV  = $(SPV_DIR)/shader.rmiss.spv
RCHIT_SPV  = $(SPV_DIR)/shader.rchit.spv

.PHONY: all run clean shaders

all: shaders $(TARGET)

shaders: $(SPV_DIR) $(RGEN_SPV) $(RMISS_SPV) $(RCHIT_SPV)

$(SPV_DIR):
	mkdir -p $(SPV_DIR)

$(RGEN_SPV): $(SHADER_DIR)/shader.rgen
	glslangValidator -V --target-env vulkan1.2 $< -o $@

$(RMISS_SPV): $(SHADER_DIR)/shader.rmiss
	glslangValidator -V --target-env vulkan1.2 $< -o $@

$(RCHIT_SPV): $(SHADER_DIR)/shader.rchit
	glslangValidator -V --target-env vulkan1.2 $< -o $@

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET) $(RGEN_SPV) $(RMISS_SPV) $(RCHIT_SPV)
