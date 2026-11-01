#include "render/LightClusters.h"
#include <algorithm>
#include <cmath>

namespace LightClusters {

void Build(const std::vector<Sphere>& lights, const Projection& p, std::vector<uint32_t>& outClusters, std::vector<uint32_t>& outIndices)
{
	outClusters.assign(kCount, 0);
	outIndices.clear();
	if (lights.empty()) return;

	std::vector<uint32_t> counts(kCount, 0);
	struct Entry { uint32_t cluster; uint32_t light; };
	std::vector<Entry> entries;

	const float logRange = std::log(p.farZ / p.nearZ);
	const auto sliceEnd = [&](uint32_t slice) { return p.nearZ * std::exp(logRange * static_cast<float>(slice) / kZ); };
	const auto sliceOf = [&](float z) {
		const float t = std::log((std::max)(z, p.nearZ) / p.nearZ) / logRange;
		return static_cast<int>(std::clamp(t * kZ, 0.0f, static_cast<float>(kZ - 1)));
	};
	const auto add = [&](uint32_t x, uint32_t y, uint32_t slice, uint32_t light) {
		const uint32_t cluster = (slice * kY + y) * kX + x;
		if (counts[cluster] < kMaxLightsPerCluster) {
			++counts[cluster];
			entries.push_back({ cluster, light });
		}
	};

	for (uint32_t i = 0; i < lights.size(); ++i) {
		const Vector3& c = lights[i].viewPosition;
		const float r = lights[i].radius;
		if (p.orthographic) {
			for (uint32_t cluster = 0; cluster < kCount; ++cluster) {
				if (counts[cluster] < kMaxLightsPerCluster) {
					++counts[cluster];
					entries.push_back({ cluster, i });
				}
			}
			continue;
		}
		if (c.z + r < p.nearZ || c.z - r > p.farZ) continue;

		const int firstSlice = sliceOf(c.z - r);
		const int lastSlice = sliceOf(c.z + r);
		for (int slice = firstSlice; slice <= lastSlice; ++slice) {
			const float zNear = sliceEnd(static_cast<uint32_t>(slice));
			const float zFar = sliceEnd(static_cast<uint32_t>(slice) + 1);
			// Largest circle of the sphere inside this slice, projected at both ends of the slice: a conservative rectangle
			const float closestZ = std::clamp(c.z, zNear, zFar);
			const float dz = c.z - closestZ;
			const float sq = r * r - dz * dz;
			if (sq < 0.0f) continue;
			const float circle = std::sqrt(sq);
			const float minX = (std::min)((c.x - circle) / zNear, (c.x - circle) / zFar) / p.tanHalfFovX;
			const float maxX = (std::max)((c.x + circle) / zNear, (c.x + circle) / zFar) / p.tanHalfFovX;
			const float minY = (std::min)((c.y - circle) / zNear, (c.y - circle) / zFar) / p.tanHalfFovY;
			const float maxY = (std::max)((c.y + circle) / zNear, (c.y + circle) / zFar) / p.tanHalfFovY;
			if (maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f) continue;

			const auto tile = [](float ndc01, uint32_t size) {
				return static_cast<uint32_t>(std::clamp(ndc01 * static_cast<float>(size), 0.0f, static_cast<float>(size - 1)));
			};
			const uint32_t x0 = tile(minX * 0.5f + 0.5f, kX);
			const uint32_t x1 = tile(maxX * 0.5f + 0.5f, kX);
			const uint32_t y0 = tile(0.5f - maxY * 0.5f, kY); // +Y is the top row
			const uint32_t y1 = tile(0.5f - minY * 0.5f, kY);
			for (uint32_t y = y0; y <= y1; ++y) {
				for (uint32_t x = x0; x <= x1; ++x) add(x, y, static_cast<uint32_t>(slice), i);
			}
		}
	}

	uint32_t offset = 0;
	std::vector<uint32_t> nextWrite(kCount, 0);
	for (uint32_t cluster = 0; cluster < kCount; ++cluster) {
		outClusters[cluster] = offset | (counts[cluster] << 24);
		nextWrite[cluster] = offset;
		offset += counts[cluster];
	}
	outIndices.resize(offset);
	for (const Entry& entry : entries) outIndices[nextWrite[entry.cluster]++] = entry.light;
}

} // namespace LightClusters
