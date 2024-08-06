#version 460
#extension GL_EXT_samplerless_texture_functions : require

const vec3 REC_709_LUMA = vec3(0.2126, 0.7152, 0.0722);

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform texture2D hdrImage;

layout (push_constant) uniform PushConstant {
    ivec2 hdriImageOffset;
} pc;

void main(){
    vec4 color = texelFetch(hdrImage, ivec2(gl_FragCoord.xy) - pc.hdriImageOffset, 0);
    float luminance = dot(color.rgb, REC_709_LUMA);
    outColor = vec4(color.rgb / (1.0 + luminance), color.a);
}