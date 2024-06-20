#version 460
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 fragPos;
layout (set=0, binding=0) uniform UniformBufferObject {
	dvec4 data;
};

layout (location = 0) out vec4 outColor;

int maxIterations = 512;

dvec2 squareImaginary(dvec2 number){
	return dvec2(
		(number.x * number.x) - (number.y * number.y),
		2.0lf * number.x * number.y
	);
}

double sqlen(dvec2 vec) {
	return (vec.x * vec.x) + (vec.y * vec.y);
}

float iterateMandelbrot(dvec2 coord){
	dvec2 z = dvec2(0,0);
	for(int i=0;i<maxIterations;i++){
		z = squareImaginary(z) + coord;
		if (sqlen(z) >= 4.0lf)
			return float(i)/maxIterations;
	}
	return 0.0;
}

void main () {
	float it = iterateMandelbrot(dvec2(
		data[0] + (data[2] - data[0]) * ((fragPos.x * 0.5lf) + 0.5lf),
		data[1] + (data[3] - data[1]) * ((fragPos.y * 0.5lf) + 0.5lf)
	));
	outColor = vec4 (
		pow(it, 3.0),
		(it - pow(it * 0.9, 3.0) - pow(it * 0.88, 10.0)) * 0.75,
		(pow(it, 1.0/2) - pow(it, 3.0) - pow(it, 10.0)) * 0.5,
		1.0
	);
}
