// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// Projection estimation for stereo displacement only. (AI-assisted)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace GSStereoProjection
{

	struct Sample
	{
		double q;
		double z;
	};

	struct Mapping
	{
		double scale = 0;
		double bias = 0;
	};

	// Fit only well-conditioned perspective samples. Quantization and a small
	// number of clipped vertices must not switch an entire mesh back to raw Z.
	inline double Tolerance(std::span<const Sample> samples)
	{
		double lo = samples.front().z, hi = lo;
		for (const Sample& s : samples)
		{
			lo = std::min(lo, s.z);
			hi = std::max(hi, s.z);
		}
		return std::max(4.0, (hi - lo) * 0.002);
	}

	inline bool ValidSamples(std::span<const Sample> samples)
	{
		return samples.size() >= 3 && std::all_of(samples.begin(), samples.end(), [](const Sample& s) {
			return std::isfinite(s.q) && std::isfinite(s.z) && s.q > 0 && s.z >= 0;
		});
	}

	inline std::optional<Mapping> FitLine(std::span<const Sample> samples)
	{
		if (samples.size() < 6)
			return std::nullopt;
		double mean_q = 0, mean_z = 0;
		double min_q = samples.front().q, max_q = min_q;
		double min_z = samples.front().z, max_z = min_z;
		for (const Sample& s : samples)
		{
			mean_q += s.q;
			mean_z += s.z;
			min_q = std::min(min_q, s.q);
			max_q = std::max(max_q, s.q);
			min_z = std::min(min_z, s.z);
			max_z = std::max(max_z, s.z);
		}
		if (max_q - min_q < max_q * 0.05 || max_z - min_z < 64)
			return std::nullopt;
		mean_q /= samples.size();
		mean_z /= samples.size();
		double variance = 0, covariance = 0;
		for (const Sample& s : samples)
		{
			variance += (s.q - mean_q) * (s.q - mean_q);
			covariance += (s.q - mean_q) * (s.z - mean_z);
		}
		if (!(variance > 0))
			return std::nullopt;
		const Mapping result{covariance / variance, mean_z - covariance / variance * mean_q};
		return std::isfinite(result.scale) && std::isfinite(result.bias) && result.scale > 0 ?
		           std::optional<Mapping>(result) :
		           std::nullopt;
	}

	inline unsigned Inliers(std::span<const Sample> samples, const Mapping& mapping, double tolerance)
	{
		return static_cast<unsigned>(std::count_if(samples.begin(), samples.end(), [&](const Sample& s) {
			return std::abs(s.z - (mapping.scale * s.q + mapping.bias)) <= tolerance;
		}));
	}

	inline std::optional<Mapping> Fit(std::span<const Sample> samples)
	{
		if (!ValidSamples(samples) || samples.size() > 64)
			return std::nullopt;
		const auto line = FitLine(samples);
		if (!line)
			return std::nullopt;
		const double tolerance = Tolerance(samples);
		if (Inliers(samples, *line, tolerance) == samples.size())
			return line;

		// Bounded consensus fit. Most draws take the least-squares fast path;
		// only rejected meshes try pairs of eight evenly distributed samples.
		Mapping best = {};
		unsigned best_count = 0;
		for (unsigned i = 0; i < 8; i++)
		{
			const Sample& a = samples[i * (samples.size() - 1) / 7];
			for (unsigned j = i + 1; j < 8; j++)
			{
				const Sample& b = samples[j * (samples.size() - 1) / 7];
				if (std::abs(a.q - b.q) < std::max(a.q, b.q) * 0.05)
					continue;
				const double scale = (a.z - b.z) / (a.q - b.q);
				const Mapping candidate{scale, a.z - scale * a.q};
				if (scale <= 0 || !std::isfinite(scale) || !std::isfinite(candidate.bias))
					continue;
				const unsigned count = Inliers(samples, candidate, tolerance);
				if (count > best_count)
				{
					best = candidate;
					best_count = count;
				}
			}
		}
		if (best_count * 5 < samples.size() * 4)
			return std::nullopt;
		std::array<Sample, 64> inliers;
		unsigned count = 0;
		for (const Sample& s : samples)
		{
			if (std::abs(s.z - (best.scale * s.q + best.bias)) <= tolerance)
				inliers[count++] = s;
		}
		const auto result = FitLine(std::span(inliers.data(), count));
		return result && Inliers(samples, *result, tolerance) * 5 >= samples.size() * 4 ? result : std::nullopt;
	}

	struct Key
	{
		std::uint32_t depth_base;
		std::uint32_t depth_format;
		std::uint32_t buffer_width;
		std::array<int, 4> scissor;
		// Scissor is a clipping rectangle, not a projection/viewport identity.
		bool operator==(const Key& other) const
		{
			return depth_base == other.depth_base && depth_format == other.depth_format && buffer_width == other.buffer_width;
		}
	};

	struct Transform
	{
		float scale;
		float bias;
		bool use_q = false;
	};

	class Tracker
	{
	public:
		void Reset() { *this = {}; }

		void InvalidateDepth(std::uint32_t depth_base)
		{
			for (Target& t : m_targets)
			{
				if (t.used && t.key.depth_base == depth_base)
					t.active.reset();
			}
		}

		void Invalidate(const Key& key)
		{
			if (Target* t = Find(key, false))
				t->active.reset();
		}

		// Give each draw one vote: a large weapon mesh must not outweigh the world.
		// Buffer width/depth ownership separate small reflection jobs from the scene.
		std::optional<Transform> Observe(const Key& key, const Mapping& mapping)
		{
			Target* t = Find(key, true);
			if (!t)
				return std::nullopt;
			t->active = mapping;
			t->seen = true;
			Remember(*t, mapping);
			for (Cluster& c : t->clusters)
			{
				if (!c.votes || (SameScale(c.mapping, mapping) && std::abs(c.mapping.bias - mapping.bias) < 256))
				{
					const unsigned votes = std::min(c.votes, 255u);
					c.mapping.scale = (c.mapping.scale * votes + mapping.scale) / (votes + 1);
					c.mapping.bias = (c.mapping.bias * votes + mapping.bias) / (votes + 1);
					c.votes = votes + 1;
					break;
				}
			}
			return Correction(*t, mapping);
		}

		std::optional<Transform> Resolve(const Key& key, std::span<const Sample> samples)
		{
			std::optional<Transform> transform;
			if (const auto mapping = Fit(samples))
			{
				transform = Observe(key, *mapping);
			}
			else
			{
				Target* t = Find(key, false);
				const auto cached_mapping = t ? Match(*t, samples) : std::nullopt;
				if (!cached_mapping)
				{
					Invalidate(key);
					return std::nullopt;
				}
				t->active = *cached_mapping;
				t->seen = true;
				transform = Correction(*t, *cached_mapping);
			}
			if (transform)
			{
				const Target* t = Find(key, false);
				// Preserve GS Z rounding for similar-slope projections, so textured
				// geometry and inherited fog/shading passes move identically. Tiny
				// Q/Z differences can otherwise break their equal-depth tests. (AI-assisted)
				// Large projection-scale changes still use Q directly to keep clipped
				// vertices from creating discontinuities in stereo displacement.
				if (!SameScale(*t->reference, *t->active))
					transform = Transform{static_cast<float>(t->reference->scale), static_cast<float>(t->reference->bias), true};
			}
			return transform;
		}

		std::optional<Transform> Inherit(const Key& key)
		{
			Target* t = Find(key, false);
			return t && t->active ? Correction(*t, *t->active) : std::nullopt;
		}

		void EndFrame()
		{
			for (Target& t : m_targets)
			{
				if (!t.used)
					continue;
				const Cluster* best = nullptr;
				unsigned best_votes = 0;
				for (const Cluster& c : t.clusters)
				{
					if (c.votes < 2)
						continue;
					unsigned votes = 0;
					for (const Cluster& other : t.clusters)
					{
						if (SameScale(c.mapping, other.mapping))
							votes += other.votes;
					}
					// Depth-biased companion passes share a slope. Prefer the least
					// biased mapping in that family rather than a near-weapon offset.
					if (votes > best_votes || (votes == best_votes && best &&
												  SameScale(c.mapping, best->mapping) && std::abs(c.mapping.bias) < std::abs(best->mapping.bias)))
					{
						best = &c;
						best_votes = votes;
					}
				}
				if (best && best_votes >= 4)
				{
					if (t.pending && SameMapping(*t.pending, best->mapping))
						t.pending_frames++;
					else
						t.pending_frames = 1;
					t.pending = best->mapping;
					unsigned reference_votes = 0;
					for (const Cluster& c : t.clusters)
					{
						if (t.reference && SameScale(c.mapping, *t.reference))
							reference_votes += c.votes;
					}
					if ((!t.reference && t.pending_frames >= 2) ||
						(t.reference && SameMapping(*t.reference, best->mapping)) ||
						(t.pending_frames >= 6 && reference_votes == 0))
						t.reference = best->mapping;
				}
				// Alternating depth buffers and empty interlaced fields are normal.
				// Preserve the pending reference until this target supplies evidence
				// again; an absent field is not evidence of a different projection.
				if (t.seen)
					t.age = 0;
				else if (++t.age > 60)
				{
					t = {};
					continue;
				}
				t.seen = false;
				for (Model& model : t.models)
				{
					if (model.used && ++model.age > 60)
						model = {};
				}
				t.clusters = {};
				t.active.reset();
			}
		}

	private:
		struct Cluster
		{
			Mapping mapping;
			unsigned votes = 0;
		};
		struct Model
		{
			Mapping mapping;
			unsigned age = 0;
			bool used = false;
		};
		struct Target
		{
			Key key = {};
			bool used = false;
			unsigned age = 0;
			unsigned pending_frames = 0;
			bool seen = false;
			std::optional<Mapping> reference;
			std::optional<Mapping> pending;
			std::optional<Mapping> active;
			std::array<Cluster, 16> clusters = {};
			std::array<Model, 16> models = {};
		};
		std::array<Target, 16> m_targets = {};

		static void Remember(Target& t, const Mapping& mapping)
		{
			Model* oldest = &t.models.front();
			for (Model& model : t.models)
			{
				if (!model.used || SameMapping(model.mapping, mapping))
				{
					model = {mapping, 0, true};
					return;
				}
				if (model.age > oldest->age)
					oldest = &model;
			}
			*oldest = {mapping, 0, true};
		}

		static std::optional<Mapping> Match(Target& t, std::span<const Sample> samples)
		{
			if (!ValidSamples(samples))
				return std::nullopt;
			const double tolerance = Tolerance(samples);
			Model* best = nullptr;
			double best_error = 0;
			bool ambiguous = false;
			for (Model& model : t.models)
			{
				if (!model.used || Inliers(samples, model.mapping, tolerance) * 5 < samples.size() * 4)
					continue;
				double error = 0;
				for (const Sample& s : samples)
					error += std::min(tolerance, std::abs(s.z - (model.mapping.scale * s.q + model.mapping.bias)));
				if (best && !SameMapping(best->mapping, model.mapping))
					ambiguous = true;
				if (!best || error < best_error)
				{
					best = &model;
					best_error = error;
				}
			}
			if (!best || ambiguous)
				return std::nullopt;
			best->age = 0;
			return best->mapping;
		}

		static bool SameScale(const Mapping& a, const Mapping& b)
		{
			return a.scale > 0 && b.scale > 0 && std::abs(a.scale - b.scale) <= std::max(a.scale, b.scale) * 0.03;
		}
		static bool SameMapping(const Mapping& a, const Mapping& b)
		{
			return SameScale(a, b) && std::abs(a.bias - b.bias) < 256;
		}
		static std::optional<Transform> Correction(const Target& t, const Mapping& mapping)
		{
			if (!t.reference)
				return std::nullopt;
			const double scale = t.reference->scale / mapping.scale;
			const double bias = t.reference->bias - scale * mapping.bias;
			// Similar mappings are left alone to avoid disparity noise from Z rounding.
			// Arbitrary STQ scaling cannot be disambiguated by the GS; this heuristic
			// needs proper testing across games before enabling it by default.
			if (!std::isfinite(scale) || !std::isfinite(bias) || scale < 1.0 / 512 || scale > 512 ||
				(scale > 0.25 && scale < 4 && std::abs(bias) < 256))
				return std::nullopt;
			return Transform{static_cast<float>(scale), static_cast<float>(bias)};
		}
		Target* Find(const Key& key, bool create)
		{
			for (Target& t : m_targets)
			{
				if (t.used && t.key == key)
					return &t;
			}
			if (create)
			{
				for (Target& t : m_targets)
				{
					if (!t.used)
					{
						t.used = true;
						t.key = key;
						return &t;
					}
				}
			}
			return nullptr;
		}
	};

} // namespace GSStereoProjection
