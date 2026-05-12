# Materials & Shading

Skewer uses a physically-based shading pipeline. Every material is defined by a BBidirectional Scattering Distribution Function (BSDF) that respects conservation of energy and reciprocity:

- The BSDF cannot reflect more light than it receives
- the light path is reversible

Unlike standard RGB shaders, Skewer's materials operate on **Spectral Curves**. This allows us to accurately simulate wavelength-dependent phenomena like **Dispersion** (rainbows) and **Absorption** that are impossible to represent with simple RGB multipliers.

By using **Hero Wavelength Sampling**, we achieve spectral accuracy without the massive performance cost of tracing multiple rays per bounce. The material system is designed to make discrete decisions (like reflecting vs. refracting) based on the Hero wavelength, while the companions follow along to gather "free" spectral data.

---

## Directory Reference

The following sections detail the implementations within the `skewer/src/materials/` directory.

### Materials

The `Material` struct is the primary data container for surface properties.

- **`visible` flag**: A unique feature in Skewer where an object can be physically present (scatters rays, casts shadows) but excluded from the camera's primary visibility pass.
    - **Use Case**: Critical for **VFX Workflows**. It allows a developer to place "bounce cards" or "lighting geometry" that illuminates the scene but doesn't appear as a solid object in the final render or the deep alpha channel.

### BSDF

This module implements the mathematical models for light-matter interaction.

#### Microfacet Models (Cook-Torrance)
For metallic and rough surfaces, Skewer implements the **Cook-Torrance** model.

- **GGX Normal Distribution (NDF)**: Skewer uses the **GGX (Trowbridge-Reitz)** distribution. We chose GGX over the older Beckmann distribution because GGX has a "long tail," which produces more realistic, soft highlights on rough materials.

<figure align="center">
  <svg width="400" height="150" viewBox="0 0 400 150" xmlns="http://www.w3.org/2000/svg">
    <path d="M 50 130 L 350 130" stroke="currentColor" stroke-width="1" />
    <path d="M 200 130 Q 200 20 220 100 T 350 125" stroke="#448aff" stroke-width="2" fill="none" />
    <path d="M 200 130 Q 200 20 180 100 T 50 125" stroke="#448aff" stroke-width="2" fill="none" />
    <text x="210" y="40" fill="#448aff" font-size="12">GGX (Long Tails)</text>
    <text x="200" y="145" fill="currentColor" font-size="10" text-anchor="middle">θ = 0 (Normal)</text>
  </svg>
</figure>

- **Roughness Mapping**: Artists input a linear roughness [0,1], which we internally square ($\alpha = \text{roughness}^2$) to provide a more intuitive, perceptually linear control.


#### Dielectrics & Spectral Dispersion
The `Dielectric` material represents transparent surfaces like glass, water, and diamonds.

- **Fresnel Equations**: We use the exact Fresnel equations (not Schlick's approximation) to calculate the probability of reflection vs. refraction.
- **Dispersion (Cauchy's Formula)**: Skewer supports spectral dispersion, where the Index of Refraction (IOR) varies by wavelength: $n(\lambda) = A + B/\lambda^2$.
- **Hero Wavelength Strategy**: Refraction angles are determined by the **Hero Wavelength**. Companion wavelengths follow the Hero's path but have their radiance zeroed if their specific IOR would have resulted in a significantly different path, prioritizing single-ray performance.

### Textures

The `ImageTexture` class handles the loading and sampling of image-based data.

- **Bilinear Filtering**: Implements bilinear interpolation for texture sampling to prevent "blocky" artifacts when close to low-resolution maps.
- **Repeat Wrapping**: Textures are automatically tiled using repeat wrapping logic.

### Texture Lookup (Shading Resolution)

This system manages the resolution of textures and the construction of surface frames.

- **Normal Mapping (TBN Frame)**: Skewer supports tangent-space normal mapping.
- **Derivatives**: During intersection, we calculate $dp/du$ and $dp/dv$ (partial derivatives of the surface position with respect to UV).
- **Gram-Schmidt Orthogonalization**: We use these derivatives to build an Orthonormal Basis (ONB) on the surface, allowing us to perturb the shading normal using texture data without introducing "black pixel" artifacts caused by non-orthogonal frames.
