shader_type spatial;
render_mode cull_back, depth_draw_opaque, diffuse_burley, depth_prepass_alpha;

uniform sampler2DArray u_texture_array : source_color, filter_nearest_mipmap_anisotropic;
varying float v_layer;

void vertex() {
    v_layer = UV2.x;
}

void fragment() {
    vec2 dx = dFdx(UV);
    vec2 dy = dFdy(UV);
    
    vec2 uv_fract = fract(UV);
    vec3 uvw = vec3(uv_fract, v_layer);

    vec4 tex = textureGrad(u_texture_array, uvw, dx, dy);
    
    if (tex.a < 0.5) {
        discard;
    }
    
    ALBEDO = tex.rgb;
}