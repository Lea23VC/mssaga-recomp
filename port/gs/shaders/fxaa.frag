#version 450
// FXAA (after Timothy Lottes' FXAA 3.11, quality preset ~12): smooths jagged edges of the presented picture. Finds an
// edge from the contrast of each pixel's luma with its neighbours, walks along it both ways to find where it ends,
// and blends across it by how far the pixel is from the edge's ends, plus a small blend for sub-pixel detail.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D src;
layout(set = 3, binding = 0) uniform Px { vec4 px; };      // xy: one window pixel in uv

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 at(vec2 uv) { return texture(src, uv).rgb; }
float lum(vec2 uv) { return luma(at(uv)); }

void main() {
    vec2 p = px.xy, uv = v_uv;
    vec3 rgbM = at(uv);
    float lM = luma(rgbM);
    float lN = lum(uv + vec2(0.0, -p.y)), lS = lum(uv + vec2(0.0, p.y));
    float lW = lum(uv + vec2(-p.x, 0.0)), lE = lum(uv + vec2(p.x, 0.0));
    float lmin = min(lM, min(min(lN, lS), min(lW, lE))), lmax = max(lM, max(max(lN, lS), max(lW, lE)));
    float range = lmax - lmin;
    if (range < max(0.0312, lmax * 0.125)) { out_color = vec4(rgbM, 1.0); return; }   // no edge here

    float lNW = lum(uv + vec2(-p.x, -p.y)), lNE = lum(uv + vec2(p.x, -p.y));
    float lSW = lum(uv + vec2(-p.x, p.y)), lSE = lum(uv + vec2(p.x, p.y));
    // sub-pixel blend: how much the pixel differs from its neighbourhood's average
    float avg = (2.0 * (lN + lS + lW + lE) + lNW + lNE + lSW + lSE) / 12.0;
    float sub = clamp(abs(avg - lM) / range, 0.0, 1.0);
    sub = smoothstep(0.0, 1.0, sub);
    sub = sub * sub * 0.75;

    // horizontal or vertical edge
    float eh = abs(lNW + lNE - 2.0 * lN) + 2.0 * abs(lW + lE - 2.0 * lM) + abs(lSW + lSE - 2.0 * lS);
    float ev = abs(lNW + lSW - 2.0 * lW) + 2.0 * abs(lN + lS - 2.0 * lM) + abs(lNE + lSE - 2.0 * lE);
    bool horz = eh >= ev;
    float step = horz ? p.y : p.x;
    float l1 = horz ? lN : lW, l2 = horz ? lS : lE;
    float g1 = abs(l1 - lM), g2 = abs(l2 - lM);
    bool first = g1 >= g2;
    float grad = 0.25 * max(g1, g2);
    if (first) step = -step;
    float lEdge = 0.5 * (lM + (first ? l1 : l2));

    // walk along the edge both ways
    vec2 e = uv, along = horz ? vec2(p.x, 0.0) : vec2(0.0, p.y);
    if (horz) e.y += step * 0.5; else e.x += step * 0.5;
    vec2 a = e - along, b = e + along;
    float la = lum(a) - lEdge, lb = lum(b) - lEdge;
    bool da = abs(la) >= grad, db = abs(lb) >= grad;
    const float steps[10] = float[](1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0, 8.0, 8.0);
    for (int i = 0; i < 10 && !(da && db); i++) {
        if (!da) { a -= along * steps[i]; la = lum(a) - lEdge; da = abs(la) >= grad; }
        if (!db) { b += along * steps[i]; lb = lum(b) - lEdge; db = abs(lb) >= grad; }
    }
    float distA = horz ? uv.x - a.x : uv.y - a.y, distB = horz ? b.x - uv.x : b.y - uv.y;
    bool nearA = distA < distB;
    float dist = min(distA, distB), span = distA + distB;
    bool smaller = lM - lEdge < 0.0;
    bool ok = ((nearA ? la : lb) < 0.0) != smaller;
    float edgeBlend = ok ? 0.5 - dist / span : 0.0;
    float blend = max(edgeBlend, sub);
    vec2 fuv = uv;
    if (horz) fuv.y += blend * step; else fuv.x += blend * step;
    out_color = vec4(at(fuv), 1.0);
}
