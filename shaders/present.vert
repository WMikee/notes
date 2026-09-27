#version 330 core
layout(location=0) in vec2 pos;
layout(location=1) in vec2 uv;
out vec2 vuv;
void main() {
    vuv = uv;
    gl_Position = vec4(pos, 0.0, 1.0);
}