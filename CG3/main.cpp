#include <OpenGL/GL.h>
#include <GLUT/GLUT.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <future>
#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/intersect.hpp"

typedef glm::vec3 Color;
typedef glm::vec3 Vec3;
typedef glm::mat4 Mat4;

enum ObjectType {
    INVALID,
    SPHERE,
    TRIANGLE
};

struct ObjectId {
    ObjectType type;
    int index;
};

struct Material {
    Vec3 ambientFactor, diffuseFactor, specularFactor;
    float shininess;
    float reflectionFactor;
    bool refract;
    float refraction;
    float refractionFactor;
};

struct Sphere {
    Vec3 center;
    float radius;
    Material *material;
};

struct Triangle {
    Vec3 vertex[3];
    Vec3 norm;
    Material *material;
};

struct Camera {
    Vec3 position;
    Vec3 at;
    Vec3 up;
    float zNear, zFar;
    float fovy;
    float aspect;
};

enum LightType {
    POINT,
    DIRECTIONAL
};

struct Light {
    LightType type;
    Vec3 position;
    float intensity;
    Color color;
};

const int DEPTH_LIMIT = 4;
int windowWidth;
int windowHeight;
Color *pixels = nullptr;
std::vector<Sphere> spheres;
std::vector<Triangle> triangles;
std::vector<Light> lights;
Camera camera;
Color bgColor;

bool findNearestObject(const Vec3 rayFrom, const Vec3 normalizedRayDir, const ObjectId excludeObjectID, bool excludeTransparentMat, ObjectId &nearestObjectID, Vec3 &nearestPos, Vec3 &nearestNorm, Material **nearestMat, bool &isInside) {
    bool found = false;
    float nearestDist = std::numeric_limits<float>::max();
    for (int i = 0; i < spheres.size(); i++) {
        if (excludeObjectID.type == SPHERE && i == excludeObjectID.index)
            continue;
        const Sphere &obj = spheres[i];
        if (excludeTransparentMat && obj.material->refract)
            continue;
        Vec3 pos, norm;
        if (glm::intersectRaySphere(rayFrom, normalizedRayDir, obj.center, obj.radius, pos, norm)) {
            float distance = glm::distance(rayFrom, pos);
            if (distance < nearestDist) {
                found = true;
                nearestDist = distance;
                nearestObjectID.type = SPHERE;
                nearestObjectID.index = i;
                nearestPos = pos;
                nearestNorm = norm;
                *nearestMat = obj.material;
                isInside = glm::distance(rayFrom, obj.center) < obj.radius;
            }
        }
    }
    for (int i = 0; i < triangles.size(); i++) {
        if (excludeObjectID.type == TRIANGLE && i == excludeObjectID.index)
            continue;
        const Triangle &obj = triangles[i];
        if (excludeTransparentMat && obj.material->refract)
            continue;
        Vec3 baryPos;
        if (glm::intersectRayTriangle(rayFrom, normalizedRayDir, obj.vertex[0], obj.vertex[1], obj.vertex[2], baryPos)) {
            // See https://github.com/g-truc/glm/issues/6
            float distance = baryPos.z;
            if (distance < nearestDist) {
                found = true;
                nearestDist = distance;
                nearestObjectID.type = TRIANGLE;
                nearestObjectID.index = i;
                nearestPos = rayFrom + normalizedRayDir * baryPos.z;
                nearestNorm = obj.norm;
                *nearestMat = obj.material;
                isInside = false;
            }
        }
    }
    return found;
}

bool isShaded(const Vec3 &rayFrom, const Vec3 &normalizedRayDir, const ObjectId &excludeObjectID) {
    ObjectId a;
    Vec3 b, c;
    Material *m;
    bool isInside;
    return findNearestObject(rayFrom, normalizedRayDir, excludeObjectID, true, a, b, c, &m, isInside);
}

Color _renderPixel(Vec3 rayFrom, Vec3 normalizedRayDir, ObjectId prevObjectID, int depth, float rIndex) {
    ObjectId objectID;
    Vec3 pos, norm;
    Material *m;
    bool isInside = false;
    if (!findNearestObject(rayFrom, normalizedRayDir, prevObjectID, false, objectID, pos, norm, &m, isInside)) {
        return bgColor;
    }
    
    Color c = bgColor * m->ambientFactor;
    Vec3 reflectionDir = glm::normalize(glm::reflect(normalizedRayDir, norm));
    if (!m->refract) {
        for (const Light &light : lights) {
            Vec3 lightDir;
            if (light.type == POINT) {
                lightDir = glm::normalize(light.position - pos);
            } else if (light.type == DIRECTIONAL) {
                lightDir = -light.position;
            }

            float s = glm::dot(norm, lightDir);
            if (s > 0.0f && !isShaded(pos, lightDir, objectID)) {
                Color diffuse(s * light.intensity);
                c += diffuse * light.color * m->diffuseFactor;
            }
            
            float t = glm::dot(lightDir, reflectionDir);
            if (t > 0.0f && !isShaded(pos, reflectionDir, objectID)) {
                Color specular = Color(powf(t, m->shininess) * light.intensity);
                c += specular * light.color * m->specularFactor;
            }
        }
    }

    if (depth < DEPTH_LIMIT) {
        c += _renderPixel(pos, reflectionDir, objectID, depth + 1, rIndex) * m->reflectionFactor;
        if (m->refract) {
            float n = rIndex / m->refraction;
            Vec3 N = norm;
            if (isInside)
                N *= -1;
            float cosI = -glm::dot(N, normalizedRayDir);
            float cosT2 = 1.0f - n * n * (1.0f - cosI * cosI);
            if (cosT2 > 0.0f) {
                Vec3 refractionDir = n * normalizedRayDir + (n * cosI - sqrtf(cosT2)) * N;
                // For refraction we don't exclude current object
                c += _renderPixel(pos + refractionDir * 1e-5f, refractionDir, {}, depth + 1, m->refraction) * m->refractionFactor;
            }
        }
    }
    return c;
}

Color renderPixel(const Vec3 &p) {
    return _renderPixel(camera.position, glm::normalize(p - camera.position), {}, 0, 1.0f);
}

void _render(Color *pixels, int width, int starty, int endy, Mat4 proj, glm::vec4 viewport) {
    Mat4 model;
    for (int y = starty; y < endy; y++) {
        for (int x = 0; x < width; x++) {
            Vec3 win = {x, y, 0};
            Vec3 p = glm::unProject(win, model, proj, viewport);
            pixels[y * width + x] = renderPixel(p);
        }
    }
}

void render(Color *pixels, int width, int height) {
    Mat4 proj = glm::perspective(camera.fovy * 3.14159265358979323846f / 180.0f, camera.aspect, camera.zNear, camera.zFar) *
        glm::lookAt(camera.position, camera.at, camera.up);
    glm::vec4 viewport(0, 0, width, height);
    const int ntasks = 4;
    std::vector<std::future<void>> tasks;
    for (int y = 0; y < height; y += height / ntasks) {
        tasks.push_back(std::async(std::launch::async, _render, pixels, width, y, y + height / ntasks, proj, viewport));
    }
    for (int i = 0; i < tasks.size(); i++) {
        tasks[i].get();
    }
}

void reshape(int width, int height) {
    windowWidth = width;
    windowHeight = height;
    glViewport(0, 0, width, height);
    gluOrtho2D(0, width, 0, height);
    if (pixels != nullptr) {
        delete [] pixels;
    }
    pixels = new Color[width * height];
    camera.aspect = (float)width / height;
}

void display(void) {
    render(pixels, windowWidth, windowHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_POINTS);
    for (int y = 0; y < windowHeight; y++) {
        for (int x = 0; x < windowWidth; x++) {
            Color c = pixels[y * windowWidth + x];
            glColor3f(c.r, c.g, c.b);
            glVertex2i(x, y);
        }
    }
    glEnd();
    glutSwapBuffers();
}

Triangle make_triangle(Vec3 v0, Vec3 v1, Vec3 v2, Material *material) {
    Triangle t {{v0, v1, v2}, glm::normalize(glm::cross(v1 - v0, v2 - v0)), material};
    return t;
}

int main(int argc, char *argv[]) {
    Material copper = {{0.329412, 0.223529, 0.027451},
        {0.780392, 0.568627, 0.113725},
        {0.992157, 0.941176, 0.807843},
        27.8974,
        0.2};
    Material chrome = {{0.25, 0.25, 0.25},
        {0.4, 0.4, 0.4},
        {0.774597, 0.774597, 0.774597},
        76.8,
        0.3};
    Material glass = {{0.25, 0.25, 0.25},
        {0.4, 0.4, 0.4},
        {0.774597, 0.774597, 0.774597},
        76.8,
        0.0};
    glass.refract = true;
    glass.refraction = 1.53f;
    glass.refractionFactor = 1.0f;
    spheres.push_back({{-0.35, 0.15, 0.0}, 0.1, &glass});
    spheres.push_back({{-0.45, 0.1, -0.25}, 0.05, &copper});
//    spheres.push_back({{0.2, 0.1, 0.0}, 0.05, &chrome});
//    spheres.push_back({{-0.2, 0.1, 0.0}, 0.05, &chrome});
    
    float w = 0.5, front = 0.3, back = -0.3, h = 0.5;
    triangles.push_back(make_triangle({-w, 0, back}, {-w, 0, front}, {w, 0, back}, &chrome));
    triangles.push_back(make_triangle({w, 0, back}, {-w, 0, front}, {w, 0, front}, &chrome));
    triangles.push_back(make_triangle({-w, h, back}, {-w, 0, back}, {w, 0, back}, &copper));
    triangles.push_back(make_triangle({w, h, back}, {-w, h, back}, {w, 0, back}, &copper));
    triangles.push_back(make_triangle({-w, h, back}, {-w, 0, front}, {-w, 0, back}, &copper));
    triangles.push_back(make_triangle({-w, h, front}, {-w, 0, front}, {-w, h, back}, &copper));

    camera.position = {0.0, 0.2, 0.5};
    camera.at = {0.0, 0.1, 0.0};
    camera.up = {0.0, 1.0, 0.0};
    camera.zNear = 0.01;
    camera.zFar = 10.0;
    camera.fovy = 60;
    bgColor = {0.0, 0.0, 0.0};
    lights.push_back({POINT, {0.0, 5.0, 0.0}, 1.0, {1.0, 1.0, 1.0}});
    lights.push_back({POINT, {0.5, 0.5, 0.5}, 0.5, {1.0, 0.0, 0.0}});
    lights.push_back({DIRECTIONAL, glm::normalize(Vec3({0.5f, -0.5f, 1.0f})), 1.0, {0.0, 1.0, 1.0}});
    lights.push_back({DIRECTIONAL, glm::normalize(Vec3({0.5f, -0.5f, -1.0f})), 1.0, {1.0, 0.0, 1.0}});
    lights.push_back({DIRECTIONAL, glm::normalize(Vec3({-0.5f, -0.5f, 0.0f})), 1.0, {1.0, 1.0, 0.0}});
    lights.push_back({DIRECTIONAL, glm::normalize(Vec3({-0.5f, -0.5f, -1.0f})), 1.0, {1.0, 1.0, 0.0}});

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(windowWidth = 1280, windowHeight = 720);
    glutCreateWindow("2009210107_Term");
    glutReshapeFunc(reshape);
    glutDisplayFunc(display);
    glutMainLoop();
    return 0;
}
