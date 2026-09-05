$input v_color0, v_texcoord0

// Dear ImGui fragment shader: font-atlas / user texture modulated by
// the vertex colour. Mirrors bgfx's fs_ocornut_imgui.sc.

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);

void main()
{
	vec4 texel = texture2D(s_tex, v_texcoord0);
	gl_FragColor = texel * v_color0;
}
