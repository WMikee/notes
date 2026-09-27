#version 330 core
out vec4 frag;
uniform vec2 offset;
uniform float zoom;
uniform float dpr;
uniform float ss;           // factor de supersampling
uniform vec2 viewport;
uniform float baseSpacing;  // espaciado base en unidades de mundo
uniform float minPx;        // espaciado mínimo en pantalla
uniform float fineOnset;    // fracción de la octava antes de que aparezcan los cuadros finos
uniform float lineWidth;    // grosor en px de pantalla (constante)
uniform vec3 paperColor;
uniform vec3 gridColor;
uniform vec3 outsideColor;
uniform vec2 pageMin;       // límite superior-izquierdo de la página (mundo)
uniform vec2 pageMax;       // límite inferior-derecho de la página (mundo)
uniform float pageRadius;   // radio de las esquinas de la página (mundo)

float grid(vec2 screen, float spacingWorld) {
    vec2 cell = (screen - offset) / (spacingWorld * zoom);
    vec2 d = abs(fract(cell + 0.5) - 0.5) * spacingWorld * zoom; // px a la línea más cercana
    float dist = min(d.x, d.y);
    float h = lineWidth * 0.5;
    return 1.0 - smoothstep(h - 0.5, h + 0.5, dist);
}

float sdRoundBox(vec2 p, vec2 half_, float r) {
    vec2 q = abs(p) - half_ + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

void main() {
    vec2 screen = vec2(gl_FragCoord.x, viewport.y * dpr * ss - gl_FragCoord.y) / (dpr * ss);
    vec2 world = (screen - offset) / zoom;

    vec2 center = (pageMin + pageMax) * 0.5;
    vec2 half_ = (pageMax - pageMin) * 0.5;
    float d = sdRoundBox(world - center, half_, pageRadius);
    float px = 1.0 / (zoom * dpr * ss);
    float inside = 1.0 - smoothstep(-px, px, d);

    float t = log2(minPx / (baseSpacing * zoom));
    float lvl = ceil(t);
    float s = baseSpacing * exp2(lvl);      // espaciado en pantalla: [minPx, 2*minPx)
    float fine = smoothstep(fineOnset, 1.0, lvl - t);  // subdivisión aparece tarde y suave

    float a = max(grid(screen, s), grid(screen, s * 0.5) * fine);
    vec3 paper = mix(paperColor, gridColor, a);
    frag = vec4(mix(outsideColor, paper, inside), 1.0);
}
