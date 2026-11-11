#version 450
// xdroid360_alias_edram: a 2_10_10_10_FLOAT render target (linear values in a half float host image)
// resolved as k_2_10_10_10. The console copies the raw bits, and the game's shaders decode them from the
// sampled value: floor(s) is the exponent and frac(s) the mantissa, with s = code * 8 / 1023 (the 10-bit
// code as unorm, times 2^3). This pass writes that s for every RGB channel, and the 2-bit alpha.
//
// The float32 -> 7e3 conversion is Xenia's DxbcShaderTranslator::UnclampedFloat32To7e3 /
// PreClampedFloat32To7e3 (xenia/gpu/dxbc_shader_translator_om.cc), itself from DirectXTex: clamp to
// [0, 31.875], denormalize below 2^-2, rebias the exponent, round to nearest even.
layout(set = 0, binding = 0) uniform sampler2D origen;
layout(push_constant) uniform Constantes {
  ivec2 desplazamiento;  // source texel = destination pixel + desplazamiento
  int modo;              // 0 = write the 7e3 codes; 1 = only clamp to the format's range (a copy to a half
                         // float texture: the console's target cannot hold more than 31.875)
} constantes;
layout(location = 0) out vec4 salida;

uint Codificar7e3(float valor) {
  // A NaN fails the comparison and becomes 0, like Xenia's max(x, 0).
  float acotado = valor > 0.0 ? min(valor, 31.875) : 0.0;
  uint f32 = floatBitsToUint(acotado);
  uint sesgado;
  if (f32 < 0x3E800000u) {
    // Too small for a normalized 7e3 (below 2^-2): denormalized.
    uint desplazar = min(125u - (f32 >> 23u), 24u);
    sesgado = ((f32 & 0x7FFFFFu) | 0x800000u) >> desplazar;
  } else {
    sesgado = f32 + 0xC2000000u;  // exponent bias: -124 << 23
  }
  return ((sesgado + 0x7FFFu + ((sesgado >> 16u) & 1u)) >> 16u) & 0x3FFu;
}

void main() {
  vec4 texel = texelFetch(origen, ivec2(gl_FragCoord.xy) + constantes.desplazamiento, 0);
  if (constantes.modo == 1) {
    vec3 rgb = vec3(texel.r > 0.0 ? min(texel.r, 31.875) : 0.0, texel.g > 0.0 ? min(texel.g, 31.875) : 0.0,
                    texel.b > 0.0 ? min(texel.b, 31.875) : 0.0);
    salida = vec4(rgb, texel.a > 0.0 ? min(texel.a, 1.0) : 0.0);
    return;
  }
  vec3 codigo = vec3(float(Codificar7e3(texel.r)), float(Codificar7e3(texel.g)), float(Codificar7e3(texel.b)));
  float alfa = texel.a > 0.0 ? min(texel.a, 1.0) : 0.0;
  salida = vec4(codigo * (8.0 / 1023.0), floor(alfa * 3.0 + 0.5) / 3.0);
}
