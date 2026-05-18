$input v_normal, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_albedo, 0);

void main() {
    vec4 albedo = texture2D(s_albedo, v_texcoord0);
    // No lighting yet — flat albedo. Directional light lands in M3.
    gl_FragColor = albedo;
}
