#version 450
// xdroid360_alias_edram: fullscreen triangle of the 7e3 encode pass (see xdroid360_codificar_7e3.frag).
// Drawn with vkCmdDraw(3 vertices); the viewport and the scissor are the destination rectangle.
void main() {
  vec2 posicion = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
  gl_Position = vec4(posicion * 2.0 - 1.0, 0.0, 1.0);
}
