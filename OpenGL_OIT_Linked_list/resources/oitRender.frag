#version 430 core
#define MAX_FRAGMENTS 75

layout (location = 0) out vec4 FragColor;
in vec3 vertexPos;
in vec3 vertexNor;
in vec2 textureCoord;

uniform vec3 cameraPos;
uniform vec3 lightPos;
uniform vec3 k;
uniform uint MaxNodes;

uniform sampler2D texture_diffuse;
uniform sampler2D texture_depth;

struct NodeType{
  vec4 color; // float * 4
  float depth; // float * 1
  uint next; // uint * 1
};

layout(binding = 0, r32ui) uniform uimage2D headPointers; // n*n 头指针
layout(binding = 0, offset = 0) uniform atomic_uint nextNodeCounter; // n*n 

layout( binding = 0, std430 ) buffer linkedLists {
  NodeType nodes[];
};


void main() {

  // 归一化当前 fragment 的屏幕坐标 (gl_FragCoord.xy)
  vec2 uv = gl_FragCoord.xy / vec2(800, 600); // 记得传入屏幕宽高！
  // 从 depth texture 中采样对应位置的深度
  float depth = texture(texture_depth, uv).r;
  // 比较 fragment 当前深度 (gl_FragCoord.z) 和 depth texture 中的深度
  
  if (gl_FragCoord.z > depth + 0.0001) { // 加一个小偏移避免浮点误差
      discard; // 丢弃当前 fragment
  }
  
  // 计数器 id (当前可以写入的 id)
  uint atomic_buffer = atomicCounterIncrement(nextNodeCounter);
  // 计数器 id 合法
  if(atomic_buffer < MaxNodes){
    // 将 headPointers[x][y] 处的原始值赋给 preHead, 并将 atomic_buffer 写入 headPointers[x][y]
    uint preHead = imageAtomicExchange(headPointers, ivec2(gl_FragCoord.xy), atomic_buffer);

    // 假如一个新的 node
    vec4 color = texture(texture_diffuse, textureCoord);

    nodes[atomic_buffer].color = color;
    nodes[atomic_buffer].depth = gl_FragCoord.z;
    nodes[atomic_buffer].next = preHead;
  }
  FragColor = texture(texture_diffuse, textureCoord);
}