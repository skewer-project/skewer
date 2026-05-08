# Materials & Shading

The materials system determines the physical look of surfaces by defining their BSDF (Bidirectional Scattering Distribution Function).

## BSDF Models

### GGX Microfacet (Metal)
For metals and rough surfaces, Skewer uses the **Cook-Torrance** model with the **GGX (Trowbridge-Reitz)** normal distribution function. 
- **Sampling**: We use importance sampling of the GGX distribution to find the half-vector $h$.
- **Geometry Term**: Smith’s shadowing-masking function is used to handle microfacet self-shadowing at grazing angles.

### Dielectrics (Glass/Water)
Dielectrics use exact Fresnel equations for reflection and refraction.
- **Dispersion**: Skewer implements wavelength-dependent Index of Refraction (IOR) using **Cauchy's Equation**: $n(\lambda) = A + B/\lambda^2$. This enables realistic rainbows and prisms.

## Hero Wavelength Sampling

To handle spectral dispersion (where different wavelengths refract at different angles) without creating "branching" rays, Skewer employs **Hero Wavelength Sampling**:

1. We sample 4 wavelengths per ray.
2. We designate one as the **Hero** wavelength.
3. The refraction angle is calculated **only** for the Hero wavelength.
4. All "companion" wavelengths follow the Hero's path.
5. In the dispersive case, the radiance for companion wavelengths is killed (zeroed out) for the refracted path, ensuring the spectral calculation remains unbiased while maintaining single-ray performance.
