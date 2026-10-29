#pragma once
// Observed as 6Vector in mangling (Vector)
struct Vector {
    float x, y, z;
    Vector() : x(0), y(0), z(0) {}
    Vector(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

struct Matrix4 {
    float m[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };
    static Matrix4 Identity() { return Matrix4{}; }
    static Matrix4 Translation(const Vector& t) {
        Matrix4 r;
        r.m[12]=t.x; r.m[13]=t.y; r.m[14]=t.z;
        return r;
    }
};
