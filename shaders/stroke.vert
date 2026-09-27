#version 330 core
layout(location=0) in vec2 pos;
layout(location=1) in vec4 col;
uniform vec2 viewport;
uniform vec2 offset;
uniform float zoom;
out vec4 vcol;
void main() {
    vec2 screen = pos * zoom + offset;
    vec2 n = screen / viewport * 2.0 - 1.0;
    vcol = col;
    gl_Position = vec4(n.x, -n.y, 0.0, 1.0);
}