#version 460
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 fragPos;
layout(push_constant, std430) uniform pc {
	vec4 data;
};

// layout (pixel_center_integer) in vec4 gl_FragCoord;

layout (location = 0) out vec4 outColor;

int maxIterations = 256;

vec2 squareImaginary(vec2 number){
	return vec2(
		pow(number.x,2) - pow(number.y,2),
		2 * number.x * number.y
	);
}

float iterateMandelbrot(vec2 coord){
	vec2 z = vec2(0,0);
	for(int i=0;i<maxIterations;i++){
		z = squareImaginary(z) + coord;
		if (length(z) > 2)
			return float(i)/maxIterations;
	}
	return 0.0;
}

void main () {
	float it   = iterateMandelbrot(fragPos * 2);
	outColor   = vec4 (it, it, it, 1.0);
}
