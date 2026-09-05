$input  a_position, a_normal, a_texcoord0
$output v_wpos, v_normal, v_texcoord0

// Shader Lab vertex stage. Edit this file while the app runs: it is
// recompiled and swapped in automatically.
//
// u_labParams.x = seconds since the app started
// u_labParams.y = framebuffer aspect ratio (width / height)

#include <bgfx_shader.sh>

uniform vec4 u_labParams;

void main() {
    float t = u_labParams.x;

    // Gentle breathing along the normal so the vertex stage is visibly
    // live too, not only the fragment colours.
    float wobble = 0.03 * sin(t * 2.0 + a_position.y * 6.0);
    vec3 pos = a_position + a_normal * wobble;

    gl_Position = mul(u_modelViewProj, vec4(pos, 1.0));
    v_wpos      = mul(u_model[0], vec4(pos, 1.0)).xyz;
    v_normal    = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
    v_texcoord0 = a_texcoord0;
}
