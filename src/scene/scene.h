// #ifndef SKWR_SCENE_SCENE_H_
// #define SKWR_SCENE_SCENE_H_

// #include <vector>
// #include "base/shape.h" // The Variant (Sphere/Triangle)
// #include "accelerators/bvh.h"

// /**
//  * ├── scene/               # The "World" Container
//     │   ├── scene.h          # Holds: vector<Shape>, vector<Light>, BVH
//     │   └── camera.h         # Camera logic
//  */

// namespace skwr
// {

//     class Scene
//     {
//     public:
//         // Called by RenderSession during setup
//         void AddShape(const Shape &shape);
//         void Build(); // Constructs the BVH from the shapes list

//         // THE CRITICAL HOT-PATH FUNCTION
//         // The Integrator calls this millions of times.
//         // Returns true if hit, and fills 'isect' with details (normal, material, UV)
//         bool Intersect(const Ray &r, Interaction *isect) const;

//         // Needed for light sampling (picking a random light)
//         const std::vector<Light> &GetLights() const;

//     private:
//         std::vector<Shape> shapes_; // Raw list of objects
//         BVH bvh_;                   // The acceleration structure
//     };

// } // namespace skwr

// #endif // SKWR_SCENE_SCENE_H_