shader_type spatial;

render_mode unshaded, cull_disabled;

void fragment() {
    ALBEDO = vec3(1.0, 1.0, 1.0);
    EMISSION = vec3(4.0, 4.0, 4.0);
    ALPHA = 0.05;
}