$input v_normal, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_albedo, 0);

// u_lightDir.xyz   = normalised direction FROM light to surface
// u_lightDir.w     = intensity
// u_lightColor.xyz = light colour
uniform vec4 u_lightDir;
uniform vec4 u_lightColor;

void main() {
    vec4 albedo = texture2D(s_albedo, v_texcoord0);
    vec3 N = normalize(v_normal);
    // Lambert: max(dot(N, -L), 0) — the shader-side direction is the
    // direction *to* surfaces, so the dot uses -u_lightDir.xyz to
    // produce the surface-to-light vector.
    float ndotl = max(dot(N, -u_lightDir.xyz), 0.0);
    vec3 diffuse = albedo.rgb * u_lightColor.rgb * (ndotl * u_lightDir.w);
    // Small ambient so unlit faces are not pure black.
    vec3 ambient = albedo.rgb * 0.15;
    gl_FragColor = vec4(ambient + diffuse, albedo.a);
}
