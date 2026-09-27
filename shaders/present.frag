#version 330 core
uniform sampler2D uTex;
uniform vec2 uTexelSize;
in vec2 vuv;
out vec4 frag;
void main() {
    vec4 acc = vec4(0.0);
    acc += texture(uTex, vuv + vec2(-uTexelSize.x, -uTexelSize.y));
    acc += texture(uTex, vuv + vec2( 0.0,          -uTexelSize.y));
    acc += texture(uTex, vuv + vec2( uTexelSize.x, -uTexelSize.y));
    acc += texture(uTex, vuv + vec2(-uTexelSize.x,  0.0));
    acc += texture(uTex, vuv + vec2( 0.0,           0.0));
    acc += texture(uTex, vuv + vec2( uTexelSize.x,  0.0));
    acc += texture(uTex, vuv + vec2(-uTexelSize.x,  uTexelSize.y));
    acc += texture(uTex, vuv + vec2( 0.0,           uTexelSize.y));
    acc += texture(uTex, vuv + vec2( uTexelSize.x,  uTexelSize.y));
    frag = acc * (1.0 / 9.0);


    frag.a = 1.0;
}
