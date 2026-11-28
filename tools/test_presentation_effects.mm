// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Standalone Metal integration checks; run through test_presentation_shaders.py. (AI-assisted)
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "../pcsx2/GS/Renderers/Metal/GSMTLSharedHeader.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct LegacyUniform
{
	vector_float4 source_rect, target_rect;
	vector_float2 source_size, target_size, target_resolution, rcp_target_resolution, source_resolution, rcp_source_resolution;
	vector_float4 time;
};
static_assert(sizeof(LegacyUniform) == 96);
static_assert(sizeof(GSMTLPresentPSUniform) == 128);

static void Check(bool condition, const char* message)
{
	if (!condition)
	{
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(1);
	}
}

struct Renderer
{
	id<MTLDevice> device = MTLCreateSystemDefaultDevice();
	id<MTLCommandQueue> queue = [device newCommandQueue];
	id<MTLSamplerState> sampler;
	id<MTLTexture> source;
	std::map<std::pair<void*, std::string>, id<MTLRenderPipelineState>> pipelines;

	Renderer()
	{
		Check(device != nil, "Metal device available");
		MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
		sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
		sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
		sampler = [device newSamplerStateWithDescriptor:sd];
		MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:128 height:64 mipmapped:NO];
		td.storageMode = device.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
		source = [device newTextureWithDescriptor:td];
		Check(source != nil && sampler != nil && queue != nil, "allocate source and sampler");
	}

	void Fixture(bool eyes)
	{
		std::vector<vector_float4> data(128 * 64);
		for (int y = 0; y < 64; y++)
			for (int x = 0; x < 128; x++)
			{
				const float v = 0.1f + 0.8f * (eyes ? static_cast<float>((x % 64) ^ y) / 63 : static_cast<float>(x) / 127);
				data[y * 128 + x] = eyes ? (x < 64 ? (vector_float4){v, 0, 0, 1} : (vector_float4){0, v, 0, 1}) : (vector_float4){v, v, v, 1};
			}
		[source replaceRegion:MTLRegionMake2D(0, 0, 128, 64) mipmapLevel:0 withBytes:data.data() bytesPerRow:128 * sizeof(vector_float4)];
	}

	std::vector<vector_float4> Render(id<MTLLibrary> library, const char* entry, const void* parameters, size_t size,
		vector_float4 rect, int width = 96, int height = 73, int repeats = 1, double* milliseconds = nullptr)
	{
		auto key = std::pair{(__bridge void*)library, std::string(entry)};
		id<MTLRenderPipelineState> pipeline = pipelines[key];
		if (!pipeline)
		{
			MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
			pd.vertexFunction = [library newFunctionWithName:@"vs_test"];
			pd.fragmentFunction = [library newFunctionWithName:[NSString stringWithUTF8String:entry]];
			Check(pd.fragmentFunction != nil, entry);
			pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
			NSError* error = nil;
			pipeline = [device newRenderPipelineStateWithDescriptor:pd error:&error];
			if (error)
				std::fprintf(stderr, "%s\n", error.description.UTF8String);
			Check(pipeline != nil, "create render pipeline");
			pipelines[key] = pipeline;
		}
		MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:width height:height mipmapped:NO];
		td.storageMode = MTLStorageModePrivate;
		td.usage = MTLTextureUsageRenderTarget;
		id<MTLTexture> output = [device newTextureWithDescriptor:td];
		id<MTLBuffer> readback = [device newBufferWithLength:width * height * sizeof(vector_float4) options:MTLResourceStorageModeShared];
		Check(output != nil && readback != nil, "allocate render target and readback buffer");
		MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
		pass.colorAttachments[0].texture = output;
		pass.colorAttachments[0].loadAction = MTLLoadActionClear;
		pass.colorAttachments[0].clearColor = MTLClearColorMake(-1, -1, -1, -1);
		pass.colorAttachments[0].storeAction = MTLStoreActionStore;
		id<MTLCommandBuffer> command = [queue commandBuffer];
		id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
		Check(encoder != nil, "create render encoder");
		[encoder setViewport:(MTLViewport){0, 0, static_cast<double>(width), static_cast<double>(height), 0, 1}];
		[encoder setScissorRect:(MTLScissorRect){0, 0, static_cast<NSUInteger>(width), static_cast<NSUInteger>(height)}];
		[encoder setRenderPipelineState:pipeline];
		[encoder setVertexBytes:&rect length:sizeof(rect) atIndex:0];
		[encoder setFragmentBytes:parameters length:size atIndex:1];
		[encoder setFragmentTexture:source atIndex:0];
		[encoder setFragmentSamplerState:sampler atIndex:0];
		for (int i = 0; i < repeats; i++)
			[encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
		[encoder endEncoding];
		id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
		[blit copyFromTexture:output
						 sourceSlice:0
						 sourceLevel:0
						sourceOrigin:MTLOriginMake(0, 0, 0)
						  sourceSize:MTLSizeMake(width, height, 1)
							toBuffer:readback
				   destinationOffset:0
			  destinationBytesPerRow:width * sizeof(vector_float4)
			destinationBytesPerImage:width * height * sizeof(vector_float4)];
		[blit endEncoding];
		[command commit];
		[command waitUntilCompleted];
		Check(command.status == MTLCommandBufferStatusCompleted, "GPU command completed");
		if (milliseconds)
			*milliseconds = (command.GPUEndTime - command.GPUStartTime) * 1000 / repeats;
		std::vector<vector_float4> pixels(width * height);
		std::memcpy(pixels.data(), readback.contents, pixels.size() * sizeof(vector_float4));
		return pixels;
	}
};

static GSMTLPresentPSUniform Parameters(uint32_t flags, vector_float4 rect = {0, 0, 1, 1}, int width = 96, int height = 73)
{
	GSMTLPresentPSUniform p{};
	p.source_rect = rect;
	p.target_rect = {0, 0, static_cast<float>(width), static_cast<float>(height)};
	p.source_resolution = {128, 64};
	p.target_resolution = {static_cast<float>(width), static_cast<float>(height)};
	p.flags = flags;
	p.shift = flags & 1 ? 0.025f : 0;
	p.tilt = flags & 1 ? -0.0125f : 0;
	p.vignette_width = 0.131f / 50;
	p.vignette_x = 0.75f;
	p.vignette_y = 0.5f;
	p.extend_border = 0.02f;
	p.cut_offset = 0.01f;
	p.vignette_offset = 0.01f;
	p.contrast_intensity = 0.92f;
	p.contrast_focus = 2;
	p.contrast_midpoint = 0.5f;
	p.gamma_strength = 1;
	p.blend_opacity = 0.22f;
	p.blend_focus = 8;
	p.blend_midpoint = 0.5f;
	p.black_point = 8.0f / 255;
	p.levels_scale = 255.0f / (255 - 8 - 16);
	p.temperature = 0.03f;
	return p;
}

static LegacyUniform Baseline(const GSMTLPresentPSUniform& p)
{
	LegacyUniform b{};
	b.source_rect = p.source_rect;
	b.target_rect = p.target_rect;
	b.source_size = (p.source_rect.zw - p.source_rect.xy) * p.source_resolution;
	b.target_size = p.target_rect.zw - p.target_rect.xy;
	b.target_resolution = p.target_resolution;
	b.rcp_target_resolution = 1.0f / p.target_resolution;
	b.source_resolution = p.source_resolution;
	b.rcp_source_resolution = 1.0f / p.source_resolution;
	return b;
}

static LegacyUniform Legacy(const GSMTLPresentPSUniform& p)
{
	LegacyUniform b{};
	b.source_rect = {0, 0, 0, 0};
	b.source_size = {p.contrast_intensity, p.contrast_focus};
	b.target_size = {p.contrast_midpoint, p.gamma_strength};
	b.target_resolution = {p.blend_opacity, p.blend_focus};
	b.rcp_target_resolution = {p.blend_midpoint, p.black_point * 255};
	b.source_resolution = {255 - p.black_point * 255 - 255 / p.levels_scale, p.temperature * 10};
	b.rcp_source_resolution = p.source_resolution;
	b.time = {(p.flags & 64) ? 2.0f : 0.0f, (p.flags & 8) ? 1.0f : 0.0f, (p.flags & 16) ? 1.0f : 0.0f, (p.flags & 32) ? 1.0f : 0.0f};
	return b;
}

static float Difference(const std::vector<vector_float4>& a, const std::vector<vector_float4>& b)
{
	float difference = 0;
	for (size_t i = 0; i < a.size(); i++)
		for (int c = 0; c < 3; c++)
			difference = std::max(difference, std::abs(a[i][c] - b[i][c]));
	return difference;
}

int main(int argc, char** argv)
{
	@autoreleasepool
	{
		Check(argc == 4, "arguments: current.metallib baseline.metallib legacy.metallib");
		Renderer renderer;
		id<MTLLibrary> libraries[3];
		for (int i = 0; i < 3; i++)
		{
			NSError* error = nil;
			libraries[i] = [renderer.device newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[i + 1]]] error:&error];
			Check(libraries[i] != nil, "load test library");
		}
		std::printf("GPU: %s\n", renderer.device.name.UTF8String);
		const char* entries[] = {"ps_present_copy", "ps_filter_scanlines", "ps_filter_diagonal", "ps_filter_triangular", "ps_filter_complex", "ps_filter_lottes", "ps_4x_rgss", "ps_automagical_supersampling"};
		int checks = 0;
		renderer.Fixture(false);
		for (const char* entry : entries)
		{
			const auto p = Parameters(0);
			const auto b = Baseline(p);
			const auto actual = renderer.Render(libraries[0], entry, &p, sizeof(p), p.source_rect);
			const auto expected = renderer.Render(libraries[1], entry, &b, sizeof(b), p.source_rect);
			if (std::string(entry) == "ps_present_copy")
				Check(actual[36 * 96 + 48][0] > 0.3f && actual[36 * 96 + 48][0] < 0.7f, "the GPU actually rendered the gray ramp");
			const float difference = Difference(actual, expected);
			std::printf("all-off %s: maximum RGB difference %.8f\n", entry, difference);
			Check(difference < 1.0f / 255, "all-off RGB matches stereoscopy baseline");
			checks++;
		}
		for (uint32_t flags : {8u, 16u, 32u, 64u, 120u})
		{
			const auto p = Parameters(flags);
			const auto b = Legacy(p);
			const auto actual = renderer.Render(libraries[0], "ps_present_copy", &p, sizeof(p), p.source_rect);
			const auto expected = renderer.Render(libraries[2], "ps_copy_present", &b, sizeof(b), p.source_rect);
			const float difference = Difference(actual, expected);
			std::printf("legacy colors/CRT flags %u: maximum RGB difference %.8f\n", flags, difference);
			Check(difference < 1.0f / 255, "color/CRT effects match legacy formulas and order");
			checks++;
		}
		renderer.Fixture(true);
		for (const char* entry : entries)
			for (uint32_t flags : {1u, 2u, 4u, 8u, 16u, 32u, 64u, 127u})
				for (vector_float4 rect : {(vector_float4){0, 0, 0.5, 1}, (vector_float4){0.0625, 0.0625, 0.5, 0.9375}})
				{
					const auto p = Parameters(flags, rect);
					const auto pixels = renderer.Render(libraries[0], entry, &p, sizeof(p), p.source_rect);
					for (const auto& pixel : pixels)
					{
						for (int c = 0; c < 4; c++)
							Check(std::isfinite(pixel[c]), "finite GPU output");
						Check(std::abs(pixel[1]) < 1e-6f && std::abs(pixel[2]) < 1e-6f, "no opposite-eye samples, including multi-tap filtering");
					}
					checks++;
				}
		renderer.Fixture(false);
		{
			const auto p = Parameters(1);
			const auto pixels = renderer.Render(libraries[0], "ps_present_copy", &p, sizeof(p), p.source_rect);
			for (int y = 0; y < 73; y++)
				for (int x = 0; x < 96; x++)
				{
					const float u = (x + 0.5f) / 96 + p.shift;
					const float v = (y + 0.5f) / 73 + p.tilt;
					const float source_x = std::clamp(u * 128 - 0.5f, 0.0f, 127.0f);
					const float expected = u < 0 || u > 1 || v < 0 || v > 1 ? 0 : 0.1f + 0.8f * source_x / 127;
					if (std::abs(pixels[y * 96 + x][0] - expected) >= 1.0f / 255)
						std::fprintf(stderr, "shift/tilt pixel (%d,%d): actual=%f expected=%f u=%f v=%f\n", x, y, pixels[y * 96 + x][0], expected, u, v);
					Check(std::abs(pixels[y * 96 + x][0] - expected) < 1.0f / 255, "shift/tilt samples the expected native eye coordinates and blacks exposed borders");
				}
			checks++;
		}
		for (const char* entry : entries)
		{
			auto p = Parameters(127);
			p.shift = 30.0f / 64;
			p.tilt = -10.0f / 64;
			p.vignette_width = 1.001f / 50;
			p.vignette_x = p.vignette_y = 2;
			p.extend_border = 0.2f;
			p.cut_offset = 200.0f / 64;
			p.vignette_offset = 250.0f / 64;
			p.black_point = 16.0f / 255;
			p.levels_scale = 255.0f / (255 - 16 - 32);
			const auto pixels = renderer.Render(libraries[0], entry, &p, sizeof(p), p.source_rect);
			for (const auto& pixel : pixels)
				for (int c = 0; c < 4; c++)
					Check(std::isfinite(pixel[c]), "supported parameter extremes remain finite");
			checks++;
		}
		std::printf("%d GPU image scenarios passed\n", checks);
		renderer.Fixture(false);
		for (uint32_t flags : {0u, 8u, 120u})
		{
			const auto p = Parameters(flags, (vector_float4){0, 0, 1, 1}, 1920, 1080);
			const auto b = Legacy(p);
			const auto baseline = Baseline(p);
			std::vector<double> current, reference;
			for (int trial = 0; trial < 9; trial++)
			{
				double a, z;
				renderer.Render(libraries[0], "ps_present_copy", &p, sizeof(p), p.source_rect, 1920, 1080, 20, &a);
				renderer.Render(libraries[flags == 0 ? 1 : 2], flags == 0 ? "ps_present_copy" : "ps_copy_present",
					flags == 0 ? &baseline : &b, sizeof(b), p.source_rect, 1920, 1080, 20, &z);
				if (trial > 1)
				{
					current.push_back(a);
					reference.push_back(z);
				}
			}
			std::sort(current.begin(), current.end());
			std::sort(reference.begin(), reference.end());
			std::printf("1920x1080 flags %u: median GPU ms/draw current=%.4f reference=%.4f\n", flags, current[3], reference[3]);
		}
	}
}
