import type { Vec3 } from "../types/scene";
import { getFile } from "./fs";

export interface NanoVDBBounds {
	min: Vec3;
	max: Vec3;
	centroid: Vec3;
	radius: number;
}

/**
 * Extracts the world-space bounding box from a NanoVDB file.
 * Instead of relying on brittle hardcoded offsets, this scanner searches
 * for grid headers and identifies the worldBBox via heuristics.
 * It scans the entire file to find the largest bounding box among all grids.
 */
export async function getNanoVDBBounds(
	dirHandle: FileSystemDirectoryHandle,
	filepath: string,
): Promise<NanoVDBBounds | null> {
	try {
		const file = await getFile(dirHandle, filepath);
		const buffer = await file.arrayBuffer();
		const view = new DataView(buffer);

		let bestBBox: NanoVDBBounds | null = null;

		// Search for magic numbers at 8-byte boundaries (standard NanoVDB alignment)
		for (let i = 0; i <= buffer.byteLength - 8; i += 8) {
			const m0 = view.getUint32(i, true);
			const m1 = view.getUint32(i + 4, true);
			
			// Check against "NanoVDB" in little-endian (0x6f6e614e)
			// Handles versions VDB0 through VDB6.
			const isVdbMagic = m0 === 0x6f6e614e && m1 >= 0x30424456 && m1 <= 0x36424456;
			
			if (isVdbMagic) {
				const gridOffset = i;
				// Scan for 6 doubles that form a valid bounding box (min <= max)
				// GridMetaData usually starts at offset 0 relative to grid start,
				// and worldBBox is typically found within the first 1024 bytes.
				const scanLimit = Math.min(buffer.byteLength - 48, gridOffset + 1024);

				for (let o = gridOffset + 32; o <= scanLimit; o += 4) {
					const minX = view.getFloat64(o + 0, true);
					const minY = view.getFloat64(o + 8, true);
					const minZ = view.getFloat64(o + 16, true);
					const maxX = view.getFloat64(o + 24, true);
					const maxY = view.getFloat64(o + 32, true);
					const maxZ = view.getFloat64(o + 40, true);

					if (minX <= maxX && minY <= maxY && minZ <= maxZ) {
						const dx = maxX - minX;
						const dy = maxY - minY;
						const dz = maxZ - minZ;

						if (dx > 1e-5 || dy > 1e-5 || dz > 1e-5) {
							const maxReasonable = 1e7;
							if (
								Math.abs(minX) < maxReasonable &&
								Math.abs(maxX) < maxReasonable &&
								Math.abs(minY) < maxReasonable &&
								Math.abs(maxY) < maxReasonable &&
								Math.abs(minZ) < maxReasonable &&
								Math.abs(maxZ) < maxReasonable
							) {
								const centroid: Vec3 = [
									(minX + maxX) / 2,
									(minY + maxY) / 2,
									(minZ + maxZ) / 2,
								];
								const radius = 0.5 * Math.max(dx, dy, dz);

								if (!bestBBox || radius > bestBBox.radius) {
									console.debug(`[nanovdb-parser] Found candidate worldBBox at grid ${gridOffset}, offset ${o}:`, { radius });
									bestBBox = {
										min: [minX, minY, minZ],
										max: [maxX, maxY, maxZ],
										centroid,
										radius,
									};
								}
							}
						}
					}
				}
				// Skip ahead to avoid re-matching the same grid's content as a magic number
				// (Though magic numbers only occur at grid starts)
				i += 32; 
			}
		}

		if (!bestBBox) {
			console.warn(`[nanovdb-parser] Could not find a valid worldBBox in ${filepath}`);
		}

		return bestBBox;
	} catch (err) {
		console.error(`[nanovdb-parser] Error parsing ${filepath}:`, err);
		return null;
	}
}
