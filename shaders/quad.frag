#version 410 core

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uTexture;

vec3 acesTonemap(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 gammaCorrect(vec3 color) {
    return pow(color, vec3(1.0 / 2.2));
}

void main() {
    vec3 hdr = texture(uTexture, vUV).rgb;
    vec3 mapped = acesTonemap(hdr);
    vec3 corrected = gammaCorrect(mapped);
    fragColor = vec4(corrected, 1.0);
}
