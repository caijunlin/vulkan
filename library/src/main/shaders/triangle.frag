#version 450

layout(location = 0) in vec2 fragTexCoord;

layout(binding = 0) uniform sampler2D videoSampler;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 color = texture(videoSampler, fragTexCoord);
    outColor = vec4(color.rgb, 1.0);
}