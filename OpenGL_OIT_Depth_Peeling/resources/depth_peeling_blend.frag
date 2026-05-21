// #version 430 core
// layout (location = 0) out vec4 FragColor;
// in vec3 vertexPos;
// in vec3 vertexNor;
// in vec2 textureCoord;

// uniform sampler2D texture_diffuse; // back

// void main() {
// 	// Bit-exact comparison between FP32 z-buffer and fragment depth
// 	// float frontDepth = texture(texture_depth, gl_FragCoord.xy).r;
// 	vec2 uv = gl_FragCoord.xy / vec2(800, 600);
// 	FragColor = texture(texture_diffuse, uv);
// 	FragColor = vec4(1.0, 0.0, 0.0, 1.0);
// }


#version 330 core
out vec4 FragColor;

in vec2 textureCoord;

uniform sampler2D texture_diffuse;

void main() { 
    vec4 color = texture(texture_diffuse, textureCoord);
    float temp = texture(texture_diffuse, textureCoord).w;
    
    FragColor = vec4(vec3(temp), 1.0f);
    FragColor = color;
}