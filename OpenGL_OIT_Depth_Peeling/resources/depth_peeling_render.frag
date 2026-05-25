#version 430 core
// 单层深度剥离：与上一层深度比较，通过则输出带光照与透明度的颜色
layout(location = 0) out vec4 FragColor;

in vec3 vertexPos;
in vec3 vertexNor;
in vec2 textureCoord;

uniform vec3 cameraPos;
uniform vec3 lightPos;
uniform vec3 k; // x=环境光, y=漫反射, z=高光

uniform sampler2D texture_diffuse;
uniform sampler2D texture_depth;
uniform vec2 u_ScreenSize;

void main() {
  vec2 uv = gl_FragCoord.xy / u_ScreenSize;
  float frontDepth = texture(texture_depth, uv).r;

  // 与上一层深度比较，更近的片段丢弃
  if (gl_FragCoord.z <= frontDepth) {
    discard;
  }

  vec3 lightColor = vec3(1.0);

  float ambientStrength = k.x;
  vec3 ambient = ambientStrength * lightColor;

  float diffuseStrength = k.y;
  vec3 normalDir = normalize(vertexNor);
  vec3 lightDir = normalize(lightPos - vertexPos);
  vec3 diffuse =
      diffuseStrength * max(dot(normalDir, lightDir), 0.0) * lightColor;

  float specularStrength = k.z;
  vec3 viewDir = normalize(cameraPos - vertexPos);
  vec3 halfwayDir = normalize(lightDir + viewDir);
  vec3 specular = specularStrength *
                  pow(max(dot(normalDir, halfwayDir), 0.0), 2.0) * lightColor;

  vec3 objectColor = vec3(0.8);
  float alpha = 0.0;
  if (textureCoord.x >= 0.0 && textureCoord.y >= 0.0) {
    vec4 sampled = texture(texture_diffuse, textureCoord);
    objectColor = sampled.rgb;
    alpha = sampled.a;
  }

  FragColor = vec4((ambient + diffuse + specular) * objectColor, alpha);
}
