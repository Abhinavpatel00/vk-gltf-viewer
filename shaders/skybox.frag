#version 460

const vec3 REC_709_LUMA = vec3(0.2126, 0.7152, 0.0722);

layout (location = 0) in vec3 fragPosition;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform samplerCube cubemapSampler;

layout (early_fragment_tests) in;

void main() {
    vec3 color = textureLod(cubemapSampler, fragPosition, 0.0).rgb;
    outColor = textureLod(cubemapSampler, fragPosition, 0.0);
}