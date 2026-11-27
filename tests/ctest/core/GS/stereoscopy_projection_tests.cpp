// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Numerical projection-policy regression tests. (AI-assisted)
#include "GS/Renderers/Common/GSStereoProjection.h"
#include <gtest/gtest.h>
#include <limits>

namespace
{
	using namespace GSStereoProjection;
	constexpr Key scene{0x1000, 0x31, 10, {0, 0, 640, 448}};

	void Learn(Tracker& tracker, const Mapping& world)
	{
		for (unsigned frame = 0; frame < 2; frame++)
		{
			for (unsigned draw = 0; draw < 8; draw++)
				tracker.Observe(scene, world);
			tracker.EndFrame();
		}
	}
} // namespace

TEST(StereoscopyProjection, FitsQuantizedPerspectiveDepth)
{
	std::array<Sample, 16> samples;
	for (unsigned i = 0; i < samples.size(); i++)
	{
		const double q = 0.001 + i * 0.002;
		samples[i] = {q, std::round(16785511 * q - 8362)};
	}
	const auto mapping = Fit(samples);
	ASSERT_TRUE(mapping);
	EXPECT_NEAR(mapping->scale, 16785511, 32);
	EXPECT_NEAR(mapping->bias, -8362, 1);
}

TEST(StereoscopyProjection, RejectsFlatInvalidAndNonPerspectiveSamples)
{
	std::array<Sample, 8> samples;
	samples.fill({1, 1000});
	EXPECT_FALSE(Fit(samples));
	for (unsigned i = 0; i < samples.size(); i++)
		samples[i] = {0.1 + i * 0.1, 1000.0 + i * i * 1000.0};
	EXPECT_FALSE(Fit(samples));
	samples[0].q = std::numeric_limits<double>::quiet_NaN();
	EXPECT_FALSE(Fit(samples));
	samples[0].q = -1;
	EXPECT_FALSE(Fit(samples));
	EXPECT_FALSE(Fit(std::span(samples.data(), 3)));
}

TEST(StereoscopyProjection, CorrectsAllFourMeasuredDepthMappings)
{
	struct Case
	{
		Mapping world;
		Mapping subject;
		double q;
	};
	const Case cases[] = {
		{{167789.27, -1086.91}, {16785511.18, -8362.09}, 0.01}, // Killzone buildings
		{{12523476.68, -2497.19}, {1252371436.50, -2506.00}, 0.004}, // GT4 slot 3
		{{1677545.77, -1113.18}, {167772715.19, -1115.49}, 0.0001}, // GT4 slot 4, signed virtual Z
		{{100663000, -12270}, {839195.47, -6555.51}, 0.1}, // COD2 hands
	};
	for (const Case& c : cases)
	{
		Tracker tracker;
		Learn(tracker, c.world);
		// A previously unseen pass appearing during play can use the learned world.
		const auto correction = tracker.Observe(scene, c.subject);
		ASSERT_TRUE(correction);
		const double original_z = c.subject.scale * c.q + c.subject.bias;
		EXPECT_NEAR(original_z * correction->scale + correction->bias,
			c.world.scale * c.q + c.world.bias, 2);
	}
}

TEST(StereoscopyProjection, DominantSlopeFamilyPrefersWorldOverBiasedWeapon)
{
	Tracker tracker;
	const Mapping world{167789, -1087}, weapon{167789, -27978}, background{16785511, -8362};
	for (unsigned frame = 0; frame < 2; frame++)
	{
		for (unsigned i = 0; i < 6; i++)
			tracker.Observe(scene, world);
		for (unsigned i = 0; i < 40; i++)
			tracker.Observe(scene, weapon);
		for (unsigned i = 0; i < 10; i++)
			tracker.Observe(scene, background);
		tracker.EndFrame();
	}
	EXPECT_FALSE(tracker.Observe(scene, world));
	const auto correction = tracker.Observe(scene, weapon);
	ASSERT_TRUE(correction);
	EXPECT_NEAR(correction->bias, 26891, 1);
}

TEST(StereoscopyProjection, IsolatesReflectionTargetsAndExpiresUnusedHistory)
{
	Tracker tracker;
	const Mapping world{100000, -1000}, background{10000000, -1000};
	Learn(tracker, world);
	Key reflection = scene;
	reflection.buffer_width = 2;
	reflection.scissor = {0, 0, 128, 256};
	EXPECT_FALSE(tracker.Observe(reflection, background));
	EXPECT_TRUE(tracker.Observe(scene, background));
	for (unsigned i = 0; i < 62; i++)
		tracker.EndFrame();
	EXPECT_FALSE(tracker.Observe(scene, background));
}

TEST(StereoscopyProjection, InheritanceStopsAtClearsAndUnrelatedDraws)
{
	Tracker tracker;
	const Mapping world{100000, -1000}, background{10000000, -1000};
	Learn(tracker, world);
	ASSERT_TRUE(tracker.Observe(scene, background));
	ASSERT_TRUE(tracker.Inherit(scene));
	tracker.InvalidateDepth(scene.depth_base);
	EXPECT_FALSE(tracker.Inherit(scene));
	EXPECT_TRUE(tracker.Observe(scene, background));
	tracker.Invalidate(scene);
	EXPECT_FALSE(tracker.Inherit(scene));
	tracker.Reset();
	EXPECT_FALSE(tracker.Observe(scene, background));
}

TEST(StereoscopyProjection, RequiresStableReferenceAndAdaptsToSceneChanges)
{
	Tracker tracker;
	const Mapping old_world{100000, -1000}, new_world{700000, -2000};
	for (unsigned i = 0; i < 8; i++)
		EXPECT_FALSE(tracker.Observe(scene, old_world));
	tracker.EndFrame();
	EXPECT_FALSE(tracker.Observe(scene, new_world));
	Learn(tracker, new_world);
	EXPECT_FALSE(tracker.Observe(scene, new_world));
	ASSERT_TRUE(tracker.Observe(scene, {70000000, -2000}));
}

TEST(StereoscopyProjection, BoundedTargetStorageFailsClosed)
{
	Tracker tracker;
	const Mapping world{100000, -1000}, background{10000000, -1000};
	for (unsigned i = 0; i < 32; i++)
	{
		Key key = scene;
		key.depth_base += i;
		EXPECT_FALSE(tracker.Observe(key, world));
	}
	Key overflow = scene;
	overflow.depth_base += 32;
	EXPECT_FALSE(tracker.Observe(overflow, background));
}

TEST(StereoscopyProjection, AlternatingDepthBuffersCompleteLearning)
{
	Tracker tracker;
	const Mapping world{1677545, -1115}, background{167772715, -1115};
	for (unsigned frame = 0; frame < 12; frame++)
	{
		Key buffer = scene;
		buffer.depth_base += frame % 2;
		for (unsigned i = 0; i < 8; i++)
			tracker.Observe(buffer, world);
		const auto correction = tracker.Observe(buffer, background);
		if (frame >= 4)
		{
			ASSERT_TRUE(correction) << frame;
			EXPECT_NEAR(correction->scale, 0.01, 0.00001);
		}
		tracker.EndFrame();
	}
}

TEST(StereoscopyProjection, EmptyFieldsAndScissorCropsPreserveHistory)
{
	Tracker tracker;
	const Mapping world{100000, -1000}, background{10000000, -1000};
	Learn(tracker, world);
	for (unsigned frame = 0; frame < 12; frame++)
	{
		tracker.EndFrame();
		Key cropped = scene;
		cropped.scissor = {120, 80, 500, 300};
		EXPECT_TRUE(tracker.Observe(cropped, background));
		for (unsigned i = 0; i < 8; i++)
			tracker.Observe(cropped, world);
		tracker.EndFrame();
	}
}

TEST(StereoscopyProjection, ExistingWorldIsNotReplacedByChangingDrawCounts)
{
	Tracker tracker;
	const Mapping world{100000, -1000}, background{10000000, -1000};
	Learn(tracker, world);
	for (unsigned frame = 0; frame < 16; frame++)
	{
		tracker.Observe(scene, world);
		for (unsigned i = 0; i < 12; i++)
			tracker.Observe(scene, background);
		tracker.EndFrame();
		const auto correction = tracker.Observe(scene, background);
		ASSERT_TRUE(correction);
		EXPECT_NEAR(correction->scale, 0.01, 0.00001);
	}
}

TEST(StereoscopyProjection, ClippedWeaponVerticesDoNotDropCorrection)
{
	Tracker tracker;
	const Mapping world{100663000, -12270}, weapon{839195, -6555};
	Learn(tracker, world);
	std::array<Sample, 64> samples;
	for (unsigned frame = 0; frame < 12; frame++)
	{
		for (unsigned i = 0; i < samples.size(); i++)
		{
			const double q = 0.06 + i * 0.004;
			samples[i] = {q, std::round(weapon.scale * q + weapon.bias)};
		}
		if (frame % 2)
		{
			// Emulate a minority of vertices whose rasterizer Z no longer follows
			// the perspective Q supplied for their texture interpolation.
			for (unsigned i = 0; i < 6; i++)
				samples[i * 7].z += 3500;
		}
		const auto correction = tracker.Resolve(scene, samples);
		ASSERT_TRUE(correction) << frame;
		EXPECT_TRUE(correction->use_q);
		EXPECT_FLOAT_EQ(correction->scale, static_cast<float>(world.scale));
		EXPECT_FLOAT_EQ(correction->bias, static_cast<float>(world.bias));
		for (unsigned i = 0; i < 8; i++)
			tracker.Observe(scene, world);
		tracker.EndFrame();
	}
}

TEST(StereoscopyProjection, WeakDrawsMustMatchAnExistingProjection)
{
	Tracker tracker;
	const Mapping world{100000, -1000}, background{10000000, -1000};
	Learn(tracker, world);
	tracker.Observe(scene, background);
	std::array<Sample, 6> samples;
	for (unsigned i = 0; i < samples.size(); i++)
	{
		const double q = 0.1 + i * 0.0000001;
		samples[i] = {q, std::round(background.scale * q + background.bias)};
	}
	EXPECT_FALSE(GSStereoProjection::Fit(samples));
	const auto correction = tracker.Resolve(scene, samples);
	ASSERT_TRUE(correction);
	EXPECT_TRUE(correction->use_q);
	for (Sample& sample : samples)
		sample.z += 100000;
	EXPECT_FALSE(tracker.Resolve(scene, samples));
	EXPECT_FALSE(tracker.Inherit(scene));
}

// Bias-only meshes need identical displacement in every material pass. (AI-assisted)
TEST(StereoscopyProjection, BiasedWeaponAndInheritedShadingPreserveDepthRounding)
{
	Tracker tracker;
	const Mapping world{167817.34, -574.58}, weapon{168066, -27976};
	Learn(tracker, world);
	std::array<Sample, 16> samples;
	for (unsigned frame = 0; frame < 12; frame++)
	{
		for (unsigned i = 0; i < samples.size(); i++)
		{
			const double q = 1.0 + i * 0.025 + frame * 0.0003;
			samples[i] = {q, std::floor(weapon.scale * q + weapon.bias)};
		}
		const auto color = tracker.Resolve(scene, samples);
		ASSERT_TRUE(color) << frame;
		EXPECT_FALSE(color->use_q);
		const auto shading = tracker.Inherit(scene);
		ASSERT_TRUE(shading);
		EXPECT_FALSE(shading->use_q);
		EXPECT_FLOAT_EQ(color->scale, shading->scale);
		EXPECT_FLOAT_EQ(color->bias, shading->bias);
		// A depth/coverage pass with the same vertices must resolve identically.
		const auto mask = tracker.Resolve(scene, samples);
		ASSERT_TRUE(mask);
		EXPECT_FALSE(mask->use_q);
		EXPECT_FLOAT_EQ(color->scale, mask->scale);
		EXPECT_FLOAT_EQ(color->bias, mask->bias);
		for (const Sample& s : samples)
		{
			EXPECT_NEAR(s.z * color->scale + color->bias,
				world.scale * s.q + world.bias, 2);
		}
		for (unsigned i = 0; i < 8; i++)
			tracker.Observe(scene, world);
		tracker.EndFrame();
	}
}

TEST(StereoscopyProjection, WeakBiasOnlyCompanionsKeepAffineDisplacement)
{
	Tracker tracker;
	const Mapping world{167817, -575}, weapon{168066, -27976};
	Learn(tracker, world);
	ASSERT_TRUE(tracker.Observe(scene, weapon));
	std::array<Sample, 3> samples;
	for (unsigned i = 0; i < samples.size(); i++)
	{
		const double q = 1.0 + i * 0.00001;
		samples[i] = {q, std::floor(weapon.scale * q + weapon.bias)};
	}
	ASSERT_FALSE(Fit(samples));
	const auto correction = tracker.Resolve(scene, samples);
	ASSERT_TRUE(correction);
	EXPECT_FALSE(correction->use_q);
	const auto shading = tracker.Inherit(scene);
	ASSERT_TRUE(shading);
	EXPECT_FLOAT_EQ(correction->scale, shading->scale);
	EXPECT_FLOAT_EQ(correction->bias, shading->bias);
}

TEST(StereoscopyProjection, AConfirmedNewSceneCanReplaceAnAbsentReference)
{
	Tracker tracker;
	const Mapping old_world{100000, -1000}, new_world{1000000, -2000};
	Learn(tracker, old_world);
	for (unsigned frame = 0; frame < 6; frame++)
	{
		for (unsigned draw = 0; draw < 8; draw++)
			tracker.Observe(scene, new_world);
		tracker.EndFrame();
	}
	EXPECT_FALSE(tracker.Observe(scene, new_world));
	EXPECT_TRUE(tracker.Observe(scene, {100000000, -2000}));
}
