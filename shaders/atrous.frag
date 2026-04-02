#version 410 core

in vec2 vUV;
layout(location = 0) out vec4 outColor;

uniform sampler2D uColorTex;
uniform sampler2D uNormalTex;
uniform sampler2D uDepthTex;

uniform int   uStepWidth;
uniform float uSigmaNormal;
uniform float uInvSigmaColor;
uniform float uInvSigmaDepth;
uniform vec2  uTexelSize;

const float kernel[3] = float[3](1.0, 2.0/3.0, 1.0/6.0);

vec3 octDecode(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec2 centerUV = vUV;

    vec3 cCenter = texture(uColorTex, centerUV).rgb;
    vec3 nCenter = octDecode(texture(uNormalTex, centerUV).rg);
    float dCenter = texture(uDepthTex, centerUV).r;

    float lumCenter = luminance(cCenter);

    vec3  sum = cCenter;
    float wSum = 1.0;

    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx == 0 && dy == 0) continue;

            vec2 offset = vec2(float(dx), float(dy)) * float(uStepWidth) * uTexelSize;
            vec2 sampleUV = centerUV + offset;

            if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
                sampleUV.y < 0.0 || sampleUV.y > 1.0)
                continue;

            vec3  cSample = texture(uColorTex, sampleUV).rgb;
            vec3  nSample = octDecode(texture(uNormalTex, sampleUV).rg);
            float dSample = texture(uDepthTex, sampleUV).r;

            float h = kernel[abs(dx)] * kernel[abs(dy)];

            float colorDist = abs(luminance(cSample) - lumCenter);
            float wColor = exp(colorDist * uInvSigmaColor);

            float normalDot = max(dot(nCenter, nSample), 0.0);
            float wNormal = pow(normalDot, uSigmaNormal);

            float depthDist = abs(dCenter - dSample);
            float wDepth = exp(depthDist * uInvSigmaDepth);

            float w = h * wColor * wNormal * wDepth;

            sum += cSample * w;
            wSum += w;
        }
    }

    outColor = vec4(sum / wSum, 1.0);
}
