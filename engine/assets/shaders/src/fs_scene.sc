$input v_normal, v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_albedo, 0);
uniform vec4 u_lightDir;
uniform vec4 u_lightColor;
void main() {
    vec4 albedo = texture2D(s_albedo, v_texcoord0);
    float diffuse = max(dot(normalize(v_normal), -u_lightDir.xyz), 0.0);
    gl_FragColor = vec4(albedo.rgb * (vec3(0.15, 0.15, 0.15) + u_lightColor.rgb * diffuse * u_lightDir.w), albedo.a);
}
