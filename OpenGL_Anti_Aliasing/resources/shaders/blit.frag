#version 430 core
layout (location = 0) out vec4 FragColor;

in vec2 vTexCoord;

uniform sampler2D uInput;

void main() {
    FragColor = texture(uInput, vTexCoord);
}
