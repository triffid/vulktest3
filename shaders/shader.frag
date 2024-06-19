#version 460
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 fragPos;
layout(push_constant, std430) uniform pc {
	vec4 data;
};

// layout (pixel_center_integer) in vec4 gl_FragCoord;

layout (location = 0) out vec4 outColor;

int maxIterations = 256;

dvec2 squareImaginary(dvec2 number){
	return dvec2(
		(number.x * number.x) - (number.y * number.y),
		2 * number.x * number.y
	);
}

float iterateMandelbrot(vec2 coord){
	dvec2 z = dvec2(0,0);
	for(int i=0;i<maxIterations;i++){
		z = squareImaginary(z) + coord;
		if (length(z) > 2)
			return float(i)/maxIterations;
	}
	return 0.0;
}

void main () {
	vec2 point = vec2(
					data[0] + (data[2] - data[0]) * (fragPos.x * 0.5 + 0.5),
					data[1] + (data[3] - data[1]) * (fragPos.y * 0.5 + 0.5)
				 );
	float it   = iterateMandelbrot(point);
	outColor   = vec4 (it, it, it, 1.0);
}
