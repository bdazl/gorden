$input v_wpos, v_normal, v_texcoord0

// Shader Lab fragment stage. Edit this file while the app runs: it is
// recompiled and swapped in automatically. Break it on purpose to see
// the diagnostics panel — the last working program keeps rendering.
//
// u_labParams.x = seconds since the app started
// u_labParams.y = framebuffer aspect ratio (width / height)

#include <bgfx_shader.sh>

uniform vec4 u_labParams;

void main() {
    float t = u_labParams.x;
    vec3  n = normalize(v_normal);
    vec3  l = normalize(vec3(0.4, 0.8, 0.3));

    float diffuse = max(dot(n, l), 0.0);
    float bands   = 0.5 + 0.5 * sin(v_texcoord0.y * 24.0 - t * 2.0);

    // Cosine palette (Inigo Quilez style) drifting with time and UV.
    vec3 palette = 0.5 + 0.5 * cos(t + v_texcoord0.xyx * 6.2831 + vec3(0.0, 2.0, 4.0));

    vec3 color = palette * (0.25 + 0.75 * diffuse) * (0.6 + 0.4 * bands);
    gl_FragColor = vec4(color, 1.0);
}
