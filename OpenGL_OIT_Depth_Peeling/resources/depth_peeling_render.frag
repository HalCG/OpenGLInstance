#version 430 core
layout (location = 0) out vec4 FragColor;
in vec3 vertexPos;
in vec3 vertexNor;
in vec2 textureCoord;

uniform vec3 cameraPos;
uniform vec3 lightPos;
uniform vec3 k;

uniform sampler2D texture_diffuse;

uniform sampler2D texture_depth;

void main() {
	// Bit-exact comparison between FP32 z-buffer and fragment depth
	// float frontDepth = texture(texture_depth, gl_FragCoord.xy).r;
  vec2 uv = gl_FragCoord.xy / vec2(800, 600);
	float frontDepth = texture(texture_depth, uv).r;
  FragColor = vec4(frontDepth, frontDepth, frontDepth, 1.0);
	if (gl_FragCoord.z <= frontDepth) {
    // FragColor = vec4(1.0, 0.0, 0.0, 1.0);
		discard;
	}else{

  vec3 lightColor = vec3(1.0f, 1.0f, 1.0f);

  // Ambient
  // Ia = ka * La
  float ambientStrenth = k[0];
  vec3 ambient = ambientStrenth * lightColor;

  // Diffuse
  // Id = kd * max(0, normal dot light) * Ld
  float diffuseStrenth = k[1];
  vec3 normalDir = normalize(vertexNor);
  vec3 lightDir = normalize(lightPos - vertexPos);
  vec3 diffuse =
      diffuseStrenth * max(dot(normalDir, lightDir), 0.0) * lightColor;

  // Specular (Phong)
  // Is = ks * (view dot reflect)^s * Ls

  // float specularStrenth = k[2];
  // vec3 viewDir = normalize(cameraPos - vertexPos);
  // vec3 reflectDir = reflect(-lightDir, normalDir);
  // vec3 specular = specularStrenth *
  //                 pow(max(dot(viewDir, reflectDir), 0.0f), 2) * lightColor;

  // Specular (Blinn-Phong)
  // Is = ks * (normal dot halfway)^s Ls
  float specularStrenth = k[2];
  vec3 viewDir = normalize(cameraPos - vertexPos);
  vec3 halfwayDir = normalize(lightDir + viewDir);
  vec3 specular = specularStrenth *
                  pow(max(dot(normalDir, halfwayDir), 0.0f), 2) * lightColor;

  // Obejct color
  vec3 objectColor = vec3(0.8, 0.8, 0.8);
  float alpha = 0.0;
  if (textureCoord.x >= 0 && textureCoord.y >= 0) {
    objectColor = texture(texture_diffuse, textureCoord).xyz;
    alpha = texture(texture_diffuse, textureCoord).w;
  }
  FragColor = vec4((ambient + diffuse + specular) * objectColor, alpha);
  }
}