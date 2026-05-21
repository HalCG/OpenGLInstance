#version 430 core
layout (location = 0) out vec4 FragColor;
in vec3 vertexPos;
in vec3 vertexNor;
in vec2 textureCoord;

uniform vec3 background_color;
uniform sampler2D texture_diffuse; // back


void main() {
	// Bit-exact comparison between FP32 z-buffer and fragment depth
	// float frontDepth = texture(texture_depth, gl_FragCoord.xy).r;
	// vec2 uv = gl_FragCoord.xy / vec2(800, 600);
	FragColor = texture(texture_diffuse, textureCoord);
	vec4 frontColor = texture(texture_diffuse, textureCoord);
	FragColor = frontColor + vec4(background_color, 1.0) * frontColor.a;
	FragColor.a = 1.0;
	// FragColor = vec4(frontColor.a, frontColor.a, frontColor.a, 1.0);
	// FragColor = vec4(background_color, 1.0);

}