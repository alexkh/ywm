#pragma once
#include <stdint.h>

template<typename Type>
class vec1 {
public:
	union {
		Type arr[1];
		struct { Type x; }; // space coordinates
		struct { Type s; }; // texture coordinates
		struct { Type i; }; // matrix coordinates
		struct { Type m; }; // matrix size
		struct { Type r; }; // colors
	};

	const Type &operator[](uint8_t i) const {
		return arr[i];
	}

	Type &operator[](uint8_t i) {
		return arr[i];
	}

	operator Type *() {
		return (Type *)this;
	}

	operator Type *const() const {
		return (Type *const)this;
	}
};

template<typename Type>
class vec2 {
public:
	union {
		Type arr[2];
		struct { Type x, y; }; // space coordinates
		struct { Type s, t; }; // texture coordinates
		struct { Type i, j; }; // matrix coordinates
		struct { Type m, n; }; // matrix size
	};

	const Type &operator[](uint8_t i) const {
		return arr[i];
	}

	Type &operator[](uint8_t i) {
		return arr[i];
	}

	operator Type *() {
		return (Type *)this;
	}

	operator Type *const() const {
		return (Type *const)this;
	}
};

template<typename Type>
class vec3 {
public:
	union {
		Type arr[3];
		struct { Type x, y, z; }; // space coordinates
		struct { Type s, t, u; }; // texture coordinates
		struct { Type i, j, k; }; // matrix coordinates
		struct { Type m, n, o; }; // matrix size
		struct { Type r, g, b; }; // colors
	};

	const Type &operator[](uint8_t i) const {
		return arr[i];
	}

	Type &operator[](uint8_t i) {
		return arr[i];
	}

	operator Type *() {
		return (Type *)this;
	}

	operator Type *const() const {
		return (Type *const)this;
	}
};

template<typename Type>
class vec4 {
public:
	union {
		Type arr[4];
		struct { Type x, y, z, w; }; // space coordinates
		struct { Type s, t, u, v; }; // texture coordinates
		struct { Type i, j, k, l; }; // matrix coordinates
		struct { Type m, n, o, p; }; // matrix size
		struct { Type r, g, b, a; }; // colors
	};

	const Type &operator[](uint8_t i) const {
		return arr[i];
	}

	Type &operator[](uint8_t i) {
		return arr[i];
	}

	operator Type *() {
		return (Type *)this;
	}

	operator Type *const() const {
		return (Type *const)this;
	}
};



