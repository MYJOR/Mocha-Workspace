#version 410 core

in vec2 vUV;
layout(location = 0) out vec4 outColor;

uniform sampler2D uNewFrame;
uniform sampler2D uAccumFrame;
uniform float     uBlendFactor;

void main() {
    vec3 n = texture(uNewFrame, vUV).rgb;
    vec3 a = texture(uAccumFrame, vUV).rgb;
    outColor = vec4(mix(a, n, uBlendFactor), 1.0);
}
