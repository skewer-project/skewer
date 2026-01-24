#ifndef HITTABLE_H
#define HITTABLE_H

class material;

// Struct to store information about hit
// Easy way to stuff info and avoid arguments in functions
class hit_record
{
public:
    point3 p;
    vec3 normal;
    std::shared_ptr<material> mat;
    double t;
    bool front_face;

    void set_face_normal(const ray &r, const vec3 &outward_normal)
    {
        // Set the hit record normal vector
        // Assume outward normal is unit length

        // Check direction with dot. Surface normal always points outward given point - center math
        front_face = dot(r.direction(), outward_normal) < 0;
        normal = front_face ? outward_normal : -outward_normal;
    }
};

class hittable
{
public:
    virtual ~hittable() = default; // virtual destructor with default cleanup operation
    virtual bool hit(const ray &r, interval ray_t, hit_record &rec) const = 0;
    // const = 0 ensures hittable object cannot exist as itself, must be defined by children
};

#endif