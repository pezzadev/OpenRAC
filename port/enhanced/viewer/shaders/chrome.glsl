// SPDX-License-Identifier: GPL-3.0-or-later
// Authored chrome/glass packets use GGX direct highlights and box-projected environment light.
vec4 chrome_surface(vec3 original, bool conductor) {
    bool authored_fallback = probe_mode == 0 && object_reflection_enabled == 2;
    if (object_reflection_enabled == 0 || (probe_mode != 1 && !authored_fallback)
        || probe_receiver != 2 || v_probe_selector.w >= 1.5) return vec4(original, 1);
    vec3 eye_position=(view*vec4(v_world_position,1)).xyz;
    vec3 geometric=cross(dFdx(eye_position),dFdy(eye_position));
    vec3 normal_view=orient_shading_normal(mat3(view)*v_normal,geometric,-eye_position);
    vec3 n=transpose(mat3(view))*normal_view;
    vec3 eye=probe_unit(transpose(mat3(view))*(-eye_position));
    float sun_visibility=1.,hero_visibility=1.;
    vec3 indirect=vec3(0);bool covered=false;
    // An authored fallback uses the retail reflection and reads no probe resources.
    if (!authored_fallback) {
#ifdef OPENRAC_EARLY_LIGHTING
    vec4 cached;
    // Match diffuse meshes: reuse the early pass's resolved shadows rather than filtering again.
    early_surface(eye_position,normal_view,cached,covered,hero_visibility);
    sun_visibility=cached.a;
    indirect=cached.rgb;
#else
    sun_visibility=sun_shadow_visibility(v_world_position,n,n);
    hero_visibility=hero_shadow_visibility(v_world_position,n);
    covered=sample_probe_irradiance(v_world_position,n,probe_surface_bias(n,eye),indirect);
#endif
    }
    if(!hero_shadow_applies(n)) hero_visibility=1.;
    float visibility=min(sun_visibility,hero_visibility);
    // Reuse early GI, including its directional normal response and local occlusion.
    // Original chrome is reserved for receivers without either source of indirect lighting.
    if (environment_count == 0 && !covered && !authored_fallback && !conductor) {
        return vec4(original, 1);
    }
    float roughness = object_reflection_material.x;
    vec3 f0 = vec3(object_reflection_material.y);
    if (conductor) f0 *= object_reflection_tint.rgb;
    vec3 fallback = probe_linear(original);
    if (covered || conductor) {
        vec3 radiance = covered ? max(indirect, vec3(0.)) * (probe_gi_multiplier / 3.14159265)
                                : probe_linear(clamp(v_probe_ambient, 0., 1.));
        fallback = radiance * surface_fresnel(dot(n, eye), f0);
    }
    vec3 reflection = environment_specular(v_world_position,n,eye,roughness,f0,fallback);
    reflection += surface_specular_direct(v_world_position,n,eye,v_probe_selector,visibility,roughness,f0);
    reflection *= object_reflection_material.z;
    if (!conductor) reflection *= object_reflection_tint.rgb;
    return vec4(probe_display(reflection),object_reflection_tint.a);
}

uniform vec4 object_glass_transmission; // Linear tint and clear transmission fraction.
vec4 thin_glass_surface() {
    vec3 eye_view = -(view * vec4(v_world_position, 1.)).xyz;
    vec3 eye = probe_unit(transpose(mat3(view)) * eye_view);
    vec3 n = probe_unit(v_normal);
    if (dot(n, eye) < 0.) n = -n;
    float f0 = object_reflection_material.y;
    float fresnel = dielectric_fresnel(dot(n, eye), f0);
    // The framebuffer supplies the clear transmitted scene via the destination blend factor.
    float reflected_weight = fresnel*object_reflection_tint.a;
    float tint_weight = (1.-object_glass_transmission.a)*(1.-reflected_weight);
    float opacity = reflected_weight+tint_weight;
    vec3 reflection = environment_specular(v_world_position,n,eye,object_reflection_material.x,f0,
                                            vec3(.11,.19,.25)*fresnel);
    reflection *= object_reflection_material.z*object_reflection_tint.rgb*object_reflection_tint.a;
    // Keep bright reflections through fog and premultiplication, before framebuffer clamping.
    return vec4(probe_display_unclamped(
        (reflection+object_glass_transmission.rgb*tint_weight)/max(opacity,1e-5)),opacity);
}
