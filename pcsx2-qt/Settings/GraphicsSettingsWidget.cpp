// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GraphicsSettingsWidget.h"
#include "QtUtils.h"
#include "SettingWidgetBinder.h"
#include "SettingsWindow.h"
#include <QtWidgets/QMessageBox>

#include "pcsx2/Host.h"
#include "pcsx2/Patch.h"
#include "pcsx2/GS/GS.h"
#include "pcsx2/GS/GSCapture.h"
#include "pcsx2/GS/GSUtil.h"

struct RendererInfo
{
	const char* name;
	GSRendererType type;
};

static constexpr RendererInfo s_renderer_info[] = {
	{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "Automatic (Default)"), GSRendererType::Auto},
#ifdef _WIN32
	//: Graphics backend/engine type. Leave as-is.
	{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "Direct3D 11 (Legacy)"), GSRendererType::DX11},
	//: Graphics backend/engine type. Leave as-is.
	{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "Direct3D 12"), GSRendererType::DX12},
#endif
#ifdef ENABLE_OPENGL
	//: Graphics backend/engine type. Leave as-is.
	{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "OpenGL"), GSRendererType::OGL},
#endif
#ifdef ENABLE_VULKAN
	//: Graphics backend/engine type. Leave as-is.
	{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "Vulkan"), GSRendererType::VK},
#endif
#ifdef __APPLE__
	//: Graphics backend/engine type. Leave as-is.
	{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "Metal"), GSRendererType::Metal},
#endif
	//: Graphics backend/engine type (refers to emulating the GS in software, on the CPU). Translate accordingly.
	{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "Software Renderer"), GSRendererType::SW},
	//: Null here means that this is a graphics backend that will show nothing.
	{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "Null"), GSRendererType::Null},
};

static const char* s_anisotropic_filtering_entries[] = {QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "Off (Default)"),
	QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "2x"), QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "4x"),
	QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "8x"), QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "16x"), nullptr};
static const char* s_anisotropic_filtering_values[] = {"0", "2", "4", "8", "16", nullptr};

static constexpr int DEFAULT_INTERLACE_MODE = 0;
static constexpr int DEFAULT_TV_SHADER_MODE = 0;
static constexpr int DEFAULT_CAS_SHARPNESS = 50;

GraphicsSettingsWidget::GraphicsSettingsWidget(SettingsWindow* settings_dialog, QWidget* parent)
	: SettingsWidget(settings_dialog, parent)
{
	SettingsInterface* sif = dialog()->getSettingsInterface();

	setupHeader(m_header);
	m_display_tab = setupTab(m_display, tr("Display"));
	m_hardware_rendering_tab = setupTab(m_hw, tr("Rendering"));
	m_software_rendering_tab = setupTab(m_sw, tr("Rendering"));
	m_hardware_fixes_tab = setupTab(m_fixes, tr("Hardware Fixes"));
	m_upscaling_fixes_tab = setupTab(m_upscaling, tr("Upscaling Fixes"));
	m_texture_replacement_tab = setupTab(m_texture, tr("Texture Replacement"));
	setupTab(m_post, tr("Post-Processing"));
	setupTab(m_osd, tr("OSD"));
	setupTab(m_capture, tr("Media Capture"));
	m_advanced_tab = setupTab(m_advanced, tr("Advanced"));
	setCurrentTab(m_hardware_rendering_tab); // TODO REMOVE rendering tab change

	//////////////////////////////////////////////////////////////////////////
	// Display Settings
	//////////////////////////////////////////////////////////////////////////
	SettingWidgetBinder::BindWidgetToEnumSetting(
		sif, m_display.aspectRatio, "EmuCore/GS", "AspectRatio", Pcsx2Config::GSOptions::AspectRatioNames, AspectRatioType::RAuto4_3_3_2);
	SettingWidgetBinder::BindWidgetToEnumSetting(sif, m_display.fmvAspectRatio, "EmuCore/GS", "FMVAspectRatioSwitch",
		Pcsx2Config::GSOptions::FMVAspectRatioSwitchNames, FMVAspectRatioSwitchType::Off);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_display.interlacing, "EmuCore/GS", "deinterlace_mode", DEFAULT_INTERLACE_MODE);
	SettingWidgetBinder::BindWidgetToIntSetting(
		sif, m_display.bilinearFiltering, "EmuCore/GS", "linear_present_mode", static_cast<int>(GSPostBilinearMode::BilinearSmooth));
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_display.widescreenPatches, "EmuCore", "EnableWideScreenPatches", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_display.noInterlacingPatches, "EmuCore", "EnableNoInterlacingPatches", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_display.integerScaling, "EmuCore/GS", "IntegerScaling", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_display.PCRTCOffsets, "EmuCore/GS", "pcrtc_offsets", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_display.PCRTCOverscan, "EmuCore/GS", "pcrtc_overscan", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_display.PCRTCAntiBlur, "EmuCore/GS", "pcrtc_antiblur", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_display.disableInterlaceOffset, "EmuCore/GS", "disable_interlace_offset", false);
	SettingWidgetBinder::BindWidgetToIntSetting(
		sif, m_capture.screenshotSize, "EmuCore/GS", "ScreenshotSize", static_cast<int>(GSScreenshotSize::WindowResolution));
	SettingWidgetBinder::BindWidgetToIntSetting(
		sif, m_capture.screenshotFormat, "EmuCore/GS", "ScreenshotFormat", static_cast<int>(GSScreenshotFormat::PNG));
	SettingWidgetBinder::BindWidgetToFloatSetting(sif, m_display.stretchY, "EmuCore/GS", "StretchY", 100.0f);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_display.cropLeft, "EmuCore/GS", "CropLeft", 0);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_display.cropTop, "EmuCore/GS", "CropTop", 0);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_display.cropRight, "EmuCore/GS", "CropRight", 0);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_display.cropBottom, "EmuCore/GS", "CropBottom", 0);

	connect(
		m_display.fullscreenModes, &QComboBox::currentIndexChanged, this, &GraphicsSettingsWidget::onFullscreenModeChanged);

	//////////////////////////////////////////////////////////////////////////
	// HW Settings
	//////////////////////////////////////////////////////////////////////////
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_hw.textureFiltering, "EmuCore/GS", "filter", static_cast<int>(BiFiltering::PS2));
	SettingWidgetBinder::BindWidgetToIntSetting(
		sif, m_hw.trilinearFiltering, "EmuCore/GS", "TriFilter", static_cast<int>(TriFiltering::Automatic), -1);
	SettingWidgetBinder::BindWidgetToEnumSetting(sif, m_hw.anisotropicFiltering, "EmuCore/GS", "MaxAnisotropy",
		s_anisotropic_filtering_entries, s_anisotropic_filtering_values, "0");
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_hw.dithering, "EmuCore/GS", "dithering_ps2", 2);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.mipmapping, "EmuCore/GS", "hw_mipmap", true);
	SettingWidgetBinder::BindWidgetToIntSetting(
		sif, m_hw.blending, "EmuCore/GS", "accurate_blending_unit", static_cast<int>(AccBlendLevel::Basic));
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.enableHWFixes, "EmuCore/GS", "UserHacks", false);
	SettingWidgetBinder::BindWidgetToEnumSetting(sif, m_hw.stereoscopicMode, "EmuCore/GS", "StereoMode",
		Pcsx2Config::GSOptions::StereoModeNames, GSStereoMode::Off);
	SettingWidgetBinder::BindWidgetToEnumSetting(sif, m_hw.stereoDominantEye, "EmuCore/GS", "StereoDominantEye",
		Pcsx2Config::GSOptions::StereoDominantEyeNames, GSStereoDominantEye::None);
	SettingWidgetBinder::BindWidgetToFloatSetting(sif, m_hw.stereoSeparation, "EmuCore/GS", "StereoSeparation", 0.0f);
	SettingWidgetBinder::BindWidgetToFloatSetting(sif, m_hw.stereoConvergence, "EmuCore/GS", "StereoConvergence", 0.0f);
	SettingWidgetBinder::BindWidgetToFloatSetting(sif, m_hw.stereoDepthFactor, "EmuCore/GS", "StereoDepthFactor", 0.0f);
	SettingWidgetBinder::BindWidgetToNormalizedSetting(sif, m_hw.stereoUiDepth, "EmuCore/GS", "StereoUiDepth", 1.0f, 0.0f);
	SettingWidgetBinder::BindWidgetToNormalizedSetting(sif, m_hw.stereoUiSecondLayerDepth, "EmuCore/GS", "StereoUiSecondLayerDepth", 1.0f, 0.0f);
	connect(m_hw.stereoUiDepth, &QSlider::valueChanged, this, &GraphicsSettingsWidget::onUiDepthChanged);
	connect(m_hw.stereoUiSecondLayerDepth, &QSlider::valueChanged, this, &GraphicsSettingsWidget::onUiSecondLayerDepthChanged);
	onUiDepthChanged();
	onUiSecondLayerDepthChanged();
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoSwapEyes, "EmuCore/GS", "StereoSwapEyes", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoFlipRendering, "EmuCore/GS", "StereoFlipRendering", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoDontRenderMonoObjects, "EmuCore/GS", "StereoDontRenderMonoObjects", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectNonPositiveZ, "EmuCore/GS", "StereoRejectNonPositiveZ", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectSmallZRange, "EmuCore/GS", "StereoRejectSmallZRange", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectSpriteBlit, "EmuCore/GS", "StereoRejectSpriteBlit", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectConstantColor, "EmuCore/GS", "StereoRejectConstantColor", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectScalingDraw, "EmuCore/GS", "StereoRejectScalingDraw", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectSbsInput, "EmuCore/GS", "StereoRejectSbsInput", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectTabInput, "EmuCore/GS", "StereoRejectTabInput", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireDisplayBuffer1, "EmuCore/GS", "StereoRequireDisplayBuffer1", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireDisplayBuffer2, "EmuCore/GS", "StereoRequireDisplayBuffer2", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoFixStencilShadows, "EmuCore/GS", "StereoFixStencilShadows", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequirePerspectiveUV, "EmuCore/GS", "StereoRequirePerspectiveUV", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireZVaries, "EmuCore/GS", "StereoRequireZVaries", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireDepthActive, "EmuCore/GS", "StereoRequireDepthActive", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectSprites, "EmuCore/GS", "StereoRejectSprites", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectUiLike, "EmuCore/GS", "StereoRejectUiLike", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUiSafeDetect, "EmuCore/GS", "StereoUiSafeDetect", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUiAdvancedDetect, "EmuCore/GS", "StereoUiAdvancedDetect", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUiBackgroundDepth, "EmuCore/GS", "StereoUiBackgroundDepth", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoMasterFix, "EmuCore/GS", "StereoMasterFix", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoMasterFix1, "EmuCore/GS", "StereoMasterFix1", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoMasterFix2, "EmuCore/GS", "StereoMasterFix2", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoMasterFix3, "EmuCore/GS", "StereoMasterFix3", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoMasterFix4, "EmuCore/GS", "StereoMasterFix4", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoMasterFix5, "EmuCore/GS", "StereoMasterFix5", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoMasterFix6, "EmuCore/GS", "StereoMasterFix6", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoMasterFix7, "EmuCore/GS", "StereoMasterFix7", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoMasterFix8, "EmuCore/GS", "StereoMasterFix8", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoMasterFix9, "EmuCore/GS", "StereoMasterFix9", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoMasterFix10, "EmuCore/GS", "StereoMasterFix10", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoMasterFixTest, "EmuCore/GS", "StereoMasterFixTest", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireTextureMapping, "EmuCore/GS", "StereoRequireTextureMapping", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireAlphaBlend, "EmuCore/GS", "StereoRequireAlphaBlend", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireAlphaTest, "EmuCore/GS", "StereoRequireAlphaTest", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireUvVaries, "EmuCore/GS", "StereoRequireUvVaries", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireColorVaries, "EmuCore/GS", "StereoRequireColorVaries", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFog, "EmuCore/GS", "StereoRequireFog", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoStencilRequireDate, "EmuCore/GS", "StereoStencilRequireDate", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoStencilRequireDatm, "EmuCore/GS", "StereoStencilRequireDatm", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoStencilRequireAte, "EmuCore/GS", "StereoStencilRequireAte", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoStencilRequireAfailZbOnly, "EmuCore/GS", "StereoStencilRequireAfailZbOnly", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoStencilRequireAfailNotKeep, "EmuCore/GS", "StereoStencilRequireAfailNotKeep", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoStencilRequireZWrite, "EmuCore/GS", "StereoStencilRequireZWrite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoStencilRequireZTest, "EmuCore/GS", "StereoStencilRequireZTest", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoStencilRequireZTestGequal, "EmuCore/GS", "StereoStencilRequireZTestGequal", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoStencilRequireFbMask, "EmuCore/GS", "StereoStencilRequireFbMask", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoStencilRequireFbMaskFull, "EmuCore/GS", "StereoStencilRequireFbMaskFull", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoStencilRequireTexIsFb, "EmuCore/GS", "StereoStencilRequireTexIsFb", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFullscreenDraw, "EmuCore/GS", "StereoRejectFullscreenDraw", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFullscreenScissor, "EmuCore/GS", "StereoRejectFullscreenScissor", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFullCover, "EmuCore/GS", "StereoRejectFullCover", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectSpriteNoGaps, "EmuCore/GS", "StereoRejectSpriteNoGaps", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectTexIsFb, "EmuCore/GS", "StereoRejectTexIsFb", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectChannelShuffle, "EmuCore/GS", "StereoRejectChannelShuffle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectTextureShuffle, "EmuCore/GS", "StereoRejectTextureShuffle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFullscreenShuffle, "EmuCore/GS", "StereoRejectFullscreenShuffle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectShaderShuffle, "EmuCore/GS", "StereoRejectShaderShuffle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectShuffleAcross, "EmuCore/GS", "StereoRejectShuffleAcross", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectShuffleSame, "EmuCore/GS", "StereoRejectShuffleSame", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectChannelFetch, "EmuCore/GS", "StereoRejectChannelFetch", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectChannelFetchFb, "EmuCore/GS", "StereoRejectChannelFetchFb", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFeedbackLoop, "EmuCore/GS", "StereoRejectFeedbackLoop", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectColclip, "EmuCore/GS", "StereoRejectColclip", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectRtaCorrection, "EmuCore/GS", "StereoRejectRtaCorrection", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectRtaSourceCorrection, "EmuCore/GS", "StereoUniversalRejectRtaSourceCorrection", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectColclipHw, "EmuCore/GS", "StereoUniversalRejectColclipHw", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectColclip, "EmuCore/GS", "StereoUniversalRejectColclip", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectBlendMix, "EmuCore/GS", "StereoUniversalRejectBlendMix", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectPabe, "EmuCore/GS", "StereoUniversalRejectPabe", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectFbMask, "EmuCore/GS", "StereoUniversalRejectFbMask", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectTexIsFb, "EmuCore/GS", "StereoUniversalRejectTexIsFb", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectNoColor, "EmuCore/GS", "StereoUniversalRejectNoColor", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectNoColor1, "EmuCore/GS", "StereoUniversalRejectNoColor1", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectAemFmt, "EmuCore/GS", "StereoUniversalRejectAemFmt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectPalFmt, "EmuCore/GS", "StereoUniversalRejectPalFmt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectDstFmt, "EmuCore/GS", "StereoUniversalRejectDstFmt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectDepthFmt, "EmuCore/GS", "StereoUniversalRejectDepthFmt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectAem, "EmuCore/GS", "StereoUniversalRejectAem", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectFba, "EmuCore/GS", "StereoUniversalRejectFba", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectFog, "EmuCore/GS", "StereoUniversalRejectFog", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectIip, "EmuCore/GS", "StereoUniversalRejectIip", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectDate, "EmuCore/GS", "StereoUniversalRejectDate", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectAtst, "EmuCore/GS", "StereoUniversalRejectAtst", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectAfail, "EmuCore/GS", "StereoUniversalRejectAfail", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectFst, "EmuCore/GS", "StereoUniversalRejectFst", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectTfx, "EmuCore/GS", "StereoUniversalRejectTfx", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectTcc, "EmuCore/GS", "StereoUniversalRejectTcc", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectWms, "EmuCore/GS", "StereoUniversalRejectWms", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectWmt, "EmuCore/GS", "StereoUniversalRejectWmt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectAdjs, "EmuCore/GS", "StereoUniversalRejectAdjs", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectAdjt, "EmuCore/GS", "StereoUniversalRejectAdjt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectLtf, "EmuCore/GS", "StereoUniversalRejectLtf", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectShuffle, "EmuCore/GS", "StereoUniversalRejectShuffle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectShuffleSame, "EmuCore/GS", "StereoUniversalRejectShuffleSame", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectReal16Src, "EmuCore/GS", "StereoUniversalRejectReal16Src", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectProcessBa, "EmuCore/GS", "StereoUniversalRejectProcessBa", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectProcessRg, "EmuCore/GS", "StereoUniversalRejectProcessRg", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectShuffleAcross, "EmuCore/GS", "StereoUniversalRejectShuffleAcross", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectWriteRg, "EmuCore/GS", "StereoUniversalRejectWriteRg", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectBlendA, "EmuCore/GS", "StereoUniversalRejectBlendA", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectBlendB, "EmuCore/GS", "StereoUniversalRejectBlendB", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectBlendC, "EmuCore/GS", "StereoUniversalRejectBlendC", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectBlendD, "EmuCore/GS", "StereoUniversalRejectBlendD", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectFixedOneA, "EmuCore/GS", "StereoUniversalRejectFixedOneA", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectBlendHw, "EmuCore/GS", "StereoUniversalRejectBlendHw", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectAMasked, "EmuCore/GS", "StereoUniversalRejectAMasked", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectRoundInv, "EmuCore/GS", "StereoUniversalRejectRoundInv", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectChannel, "EmuCore/GS", "StereoUniversalRejectChannel", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectChannelFb, "EmuCore/GS", "StereoUniversalRejectChannelFb", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectDither, "EmuCore/GS", "StereoUniversalRejectDither", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectDitherAdjust, "EmuCore/GS", "StereoUniversalRejectDitherAdjust", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectZClamp, "EmuCore/GS", "StereoUniversalRejectZClamp", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectZFloor, "EmuCore/GS", "StereoUniversalRejectZFloor", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectTCOffsetHack, "EmuCore/GS", "StereoUniversalRejectTCOffsetHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectUrbanChaosHle, "EmuCore/GS", "StereoUniversalRejectUrbanChaosHle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectTalesOfAbyssHle, "EmuCore/GS", "StereoUniversalRejectTalesOfAbyssHle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectAutomaticLod, "EmuCore/GS", "StereoUniversalRejectAutomaticLod", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectManualLod, "EmuCore/GS", "StereoUniversalRejectManualLod", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectPointSampler, "EmuCore/GS", "StereoUniversalRejectPointSampler", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectRegionRect, "EmuCore/GS", "StereoUniversalRejectRegionRect", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRejectScanmask, "EmuCore/GS", "StereoUniversalRejectScanmask", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireRtaCorrection, "EmuCore/GS", "StereoUniversalRequireRtaCorrection", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireRtaSourceCorrection, "EmuCore/GS", "StereoUniversalRequireRtaSourceCorrection", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireColclipHw, "EmuCore/GS", "StereoUniversalRequireColclipHw", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireColclip, "EmuCore/GS", "StereoUniversalRequireColclip", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireBlendMix, "EmuCore/GS", "StereoUniversalRequireBlendMix", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequirePabe, "EmuCore/GS", "StereoUniversalRequirePabe", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireFbMask, "EmuCore/GS", "StereoUniversalRequireFbMask", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireTexIsFb, "EmuCore/GS", "StereoUniversalRequireTexIsFb", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireNoColor, "EmuCore/GS", "StereoUniversalRequireNoColor", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireNoColor1, "EmuCore/GS", "StereoUniversalRequireNoColor1", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireAemFmt, "EmuCore/GS", "StereoUniversalRequireAemFmt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequirePalFmt, "EmuCore/GS", "StereoUniversalRequirePalFmt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireDstFmt, "EmuCore/GS", "StereoUniversalRequireDstFmt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireDepthFmt, "EmuCore/GS", "StereoUniversalRequireDepthFmt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireAem, "EmuCore/GS", "StereoUniversalRequireAem", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireFba, "EmuCore/GS", "StereoUniversalRequireFba", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireFog, "EmuCore/GS", "StereoUniversalRequireFog", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireIip, "EmuCore/GS", "StereoUniversalRequireIip", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireDate, "EmuCore/GS", "StereoUniversalRequireDate", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireAtst, "EmuCore/GS", "StereoUniversalRequireAtst", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireAfail, "EmuCore/GS", "StereoUniversalRequireAfail", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireFst, "EmuCore/GS", "StereoUniversalRequireFst", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireTfx, "EmuCore/GS", "StereoUniversalRequireTfx", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireTcc, "EmuCore/GS", "StereoUniversalRequireTcc", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireWms, "EmuCore/GS", "StereoUniversalRequireWms", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireWmt, "EmuCore/GS", "StereoUniversalRequireWmt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireAdjs, "EmuCore/GS", "StereoUniversalRequireAdjs", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireAdjt, "EmuCore/GS", "StereoUniversalRequireAdjt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireLtf, "EmuCore/GS", "StereoUniversalRequireLtf", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireShuffle, "EmuCore/GS", "StereoUniversalRequireShuffle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireShuffleSame, "EmuCore/GS", "StereoUniversalRequireShuffleSame", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireReal16Src, "EmuCore/GS", "StereoUniversalRequireReal16Src", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireProcessBa, "EmuCore/GS", "StereoUniversalRequireProcessBa", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireProcessRg, "EmuCore/GS", "StereoUniversalRequireProcessRg", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireShuffleAcross, "EmuCore/GS", "StereoUniversalRequireShuffleAcross", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireWriteRg, "EmuCore/GS", "StereoUniversalRequireWriteRg", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireBlendA, "EmuCore/GS", "StereoUniversalRequireBlendA", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireBlendB, "EmuCore/GS", "StereoUniversalRequireBlendB", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireBlendC, "EmuCore/GS", "StereoUniversalRequireBlendC", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireBlendD, "EmuCore/GS", "StereoUniversalRequireBlendD", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireFixedOneA, "EmuCore/GS", "StereoUniversalRequireFixedOneA", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireBlendHw, "EmuCore/GS", "StereoUniversalRequireBlendHw", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireAMasked, "EmuCore/GS", "StereoUniversalRequireAMasked", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireRoundInv, "EmuCore/GS", "StereoUniversalRequireRoundInv", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireChannel, "EmuCore/GS", "StereoUniversalRequireChannel", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireChannelFb, "EmuCore/GS", "StereoUniversalRequireChannelFb", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireDither, "EmuCore/GS", "StereoUniversalRequireDither", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireDitherAdjust, "EmuCore/GS", "StereoUniversalRequireDitherAdjust", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireZClamp, "EmuCore/GS", "StereoUniversalRequireZClamp", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireZFloor, "EmuCore/GS", "StereoUniversalRequireZFloor", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireTCOffsetHack, "EmuCore/GS", "StereoUniversalRequireTCOffsetHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireUrbanChaosHle, "EmuCore/GS", "StereoUniversalRequireUrbanChaosHle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireTalesOfAbyssHle, "EmuCore/GS", "StereoUniversalRequireTalesOfAbyssHle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireAutomaticLod, "EmuCore/GS", "StereoUniversalRequireAutomaticLod", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireManualLod, "EmuCore/GS", "StereoUniversalRequireManualLod", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequirePointSampler, "EmuCore/GS", "StereoUniversalRequirePointSampler", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireRegionRect, "EmuCore/GS", "StereoUniversalRequireRegionRect", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireScanmask, "EmuCore/GS", "StereoUniversalRequireScanmask", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireAlphaBlend, "EmuCore/GS", "StereoUniversalRequireAlphaBlend", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireAlphaTest, "EmuCore/GS", "StereoUniversalRequireAlphaTest", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireDatm, "EmuCore/GS", "StereoUniversalRequireDatm", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireZTest, "EmuCore/GS", "StereoUniversalRequireZTest", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireZWrite, "EmuCore/GS", "StereoUniversalRequireZWrite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireZTestAlways, "EmuCore/GS", "StereoUniversalRequireZTestAlways", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireZTestNever, "EmuCore/GS", "StereoUniversalRequireZTestNever", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireAa1, "EmuCore/GS", "StereoUniversalRequireAa1", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireChannelShuffle, "EmuCore/GS", "StereoUniversalRequireChannelShuffle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireTextureShuffle, "EmuCore/GS", "StereoUniversalRequireTextureShuffle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireFullscreenShuffle, "EmuCore/GS", "StereoUniversalRequireFullscreenShuffle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequirePoints, "EmuCore/GS", "StereoUniversalRequirePoints", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireLines, "EmuCore/GS", "StereoUniversalRequireLines", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireTriangles, "EmuCore/GS", "StereoUniversalRequireTriangles", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireSprites, "EmuCore/GS", "StereoUniversalRequireSprites", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireFixedQ, "EmuCore/GS", "StereoUniversalRequireFixedQ", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireFixedZ, "EmuCore/GS", "StereoUniversalRequireFixedZ", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoUniversalRequireConstantColor, "EmuCore/GS", "StereoUniversalRequireConstantColor", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectBlendMix, "EmuCore/GS", "StereoRejectBlendMix", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectPabe, "EmuCore/GS", "StereoRejectPabe", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectDither, "EmuCore/GS", "StereoRejectDither", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectScanmask, "EmuCore/GS", "StereoRejectScanmask", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectRegionRect, "EmuCore/GS", "StereoRejectRegionRect", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectNoColorOutput, "EmuCore/GS", "StereoRejectNoColorOutput", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectHleShuffle, "EmuCore/GS", "StereoRejectHleShuffle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectTCOffsetHack, "EmuCore/GS", "StereoRejectTCOffsetHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectPoints, "EmuCore/GS", "StereoRejectPoints", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectLines, "EmuCore/GS", "StereoRejectLines", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFlatShading, "EmuCore/GS", "StereoRejectFlatShading", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFst, "EmuCore/GS", "StereoRejectFst", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoEnableOptions, "EmuCore/GS", "StereoEnableOptions", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRemoveFixedSt, "EmuCore/GS", "StereoRemoveFixedSt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFixedQ, "EmuCore/GS", "StereoRejectFixedQ", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectAa1, "EmuCore/GS", "StereoRejectAa1", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectNoZTest, "EmuCore/GS", "StereoRejectNoZTest", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectNoZWrite, "EmuCore/GS", "StereoRejectNoZWrite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectZTestAlways, "EmuCore/GS", "StereoRejectZTestAlways", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectZTestNever, "EmuCore/GS", "StereoRejectZTestNever", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectAlphaTestOff, "EmuCore/GS", "StereoRejectAlphaTestOff", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectAlphaTestAlways, "EmuCore/GS", "StereoRejectAlphaTestAlways", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectAlphaTestNever, "EmuCore/GS", "StereoRejectAlphaTestNever", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectTfxModulate, "EmuCore/GS", "StereoRejectTfxModulate", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectTfxDecal, "EmuCore/GS", "StereoRejectTfxDecal", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectTfxHighlight, "EmuCore/GS", "StereoRejectTfxHighlight", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectTfxHighlight2, "EmuCore/GS", "StereoRejectTfxHighlight2", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectSmallDrawArea, "EmuCore/GS", "StereoRejectSmallDrawArea", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectWideDrawBand, "EmuCore/GS", "StereoRejectWideDrawBand", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectTopDrawBand, "EmuCore/GS", "StereoRejectTopDrawBand", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectRtSpriteNoDepth, "EmuCore/GS", "StereoRejectRtSpriteNoDepth", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectRtSpriteAlphaBlend, "EmuCore/GS", "StereoRejectRtSpriteAlphaBlend", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireProcessTexture, "EmuCore/GS", "StereoRequireProcessTexture", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectProcessTexture, "EmuCore/GS", "StereoRejectProcessTexture", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireSourceFromTarget, "EmuCore/GS", "StereoRequireSourceFromTarget", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectSourceFromTarget, "EmuCore/GS", "StereoRejectSourceFromTarget", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireDrawUsesTarget, "EmuCore/GS", "StereoRequireDrawUsesTarget", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectDrawUsesTarget, "EmuCore/GS", "StereoRejectDrawUsesTarget", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireTexIsRt, "EmuCore/GS", "StereoRequireTexIsRt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectTexIsRt, "EmuCore/GS", "StereoRejectTexIsRt", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireInTargetDraw, "EmuCore/GS", "StereoRequireInTargetDraw", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectInTargetDraw, "EmuCore/GS", "StereoRejectInTargetDraw", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireTempZ, "EmuCore/GS", "StereoRequireTempZ", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectTempZ, "EmuCore/GS", "StereoRejectTempZ", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireOneBarrier, "EmuCore/GS", "StereoRequireOneBarrier", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectOneBarrier, "EmuCore/GS", "StereoRejectOneBarrier", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFullBarrier, "EmuCore/GS", "StereoRequireFullBarrier", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFullBarrier, "EmuCore/GS", "StereoRejectFullBarrier", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireSinglePass, "EmuCore/GS", "StereoRequireSinglePass", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectSinglePass, "EmuCore/GS", "StereoRejectSinglePass", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFullscreenDrawArea, "EmuCore/GS", "StereoRequireFullscreenDrawArea", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFullscreenDrawArea, "EmuCore/GS", "StereoRejectFullscreenDrawArea", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFullscreenSprite, "EmuCore/GS", "StereoRequireFullscreenSprite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFullscreenSprite, "EmuCore/GS", "StereoRejectFullscreenSprite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireTexturedSprite, "EmuCore/GS", "StereoRequireTexturedSprite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectTexturedSprite, "EmuCore/GS", "StereoRejectTexturedSprite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireRtOutput, "EmuCore/GS", "StereoRequireRtOutput", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectRtOutput, "EmuCore/GS", "StereoRejectRtOutput", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireDepthOutput, "EmuCore/GS", "StereoRequireDepthOutput", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectDepthOutput, "EmuCore/GS", "StereoRejectDepthOutput", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireDepthRead, "EmuCore/GS", "StereoRequireDepthRead", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectDepthRead, "EmuCore/GS", "StereoRejectDepthRead", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireDepthWrite, "EmuCore/GS", "StereoRequireDepthWrite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectDepthWrite, "EmuCore/GS", "StereoRejectDepthWrite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequirePalettedTexture, "EmuCore/GS", "StereoRequirePalettedTexture", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectPalettedTexture, "EmuCore/GS", "StereoRejectPalettedTexture", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireDepthTexture, "EmuCore/GS", "StereoRequireDepthTexture", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectDepthTexture, "EmuCore/GS", "StereoRejectDepthTexture", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireMipmap, "EmuCore/GS", "StereoRequireMipmap", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectMipmap, "EmuCore/GS", "StereoRejectMipmap", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireLinearSampling, "EmuCore/GS", "StereoRequireLinearSampling", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectLinearSampling, "EmuCore/GS", "StereoRejectLinearSampling", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvActive, "EmuCore/GS", "StereoRequireFmvActive", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvActive, "EmuCore/GS", "StereoRejectFmvActive", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvHeuristic, "EmuCore/GS", "StereoRequireFmvHeuristic", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvHeuristic, "EmuCore/GS", "StereoRejectFmvHeuristic", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvSprite, "EmuCore/GS", "StereoRequireFmvSprite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvSprite, "EmuCore/GS", "StereoRejectFmvSprite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvSingleSprite, "EmuCore/GS", "StereoRequireFmvSingleSprite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvSingleSprite, "EmuCore/GS", "StereoRejectFmvSingleSprite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvTextureMapping, "EmuCore/GS", "StereoRequireFmvTextureMapping", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvTextureMapping, "EmuCore/GS", "StereoRejectFmvTextureMapping", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvProcessTexture, "EmuCore/GS", "StereoRequireFmvProcessTexture", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvProcessTexture, "EmuCore/GS", "StereoRejectFmvProcessTexture", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvFullscreenDrawArea, "EmuCore/GS", "StereoRequireFmvFullscreenDrawArea", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvFullscreenDrawArea, "EmuCore/GS", "StereoRejectFmvFullscreenDrawArea", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvFullscreenScissor, "EmuCore/GS", "StereoRequireFmvFullscreenScissor", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvFullscreenScissor, "EmuCore/GS", "StereoRejectFmvFullscreenScissor", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvNoAlphaBlend, "EmuCore/GS", "StereoRequireFmvNoAlphaBlend", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvNoAlphaBlend, "EmuCore/GS", "StereoRejectFmvNoAlphaBlend", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvNoAlphaTest, "EmuCore/GS", "StereoRequireFmvNoAlphaTest", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvNoAlphaTest, "EmuCore/GS", "StereoRejectFmvNoAlphaTest", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvNoDepthTest, "EmuCore/GS", "StereoRequireFmvNoDepthTest", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvNoDepthTest, "EmuCore/GS", "StereoRejectFmvNoDepthTest", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvNoDepthWrite, "EmuCore/GS", "StereoRequireFmvNoDepthWrite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvNoDepthWrite, "EmuCore/GS", "StereoRejectFmvNoDepthWrite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvNoDepthOutput, "EmuCore/GS", "StereoRequireFmvNoDepthOutput", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvNoDepthOutput, "EmuCore/GS", "StereoRejectFmvNoDepthOutput", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvNoDepthRead, "EmuCore/GS", "StereoRequireFmvNoDepthRead", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvNoDepthRead, "EmuCore/GS", "StereoRejectFmvNoDepthRead", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvNoFbMask, "EmuCore/GS", "StereoRequireFmvNoFbMask", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvNoFbMask, "EmuCore/GS", "StereoRejectFmvNoFbMask", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvColorOutput, "EmuCore/GS", "StereoRequireFmvColorOutput", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvColorOutput, "EmuCore/GS", "StereoRejectFmvColorOutput", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvSourceNotFromTarget, "EmuCore/GS", "StereoRequireFmvSourceNotFromTarget", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvSourceNotFromTarget, "EmuCore/GS", "StereoRejectFmvSourceNotFromTarget", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvDrawMatchesTex, "EmuCore/GS", "StereoRequireFmvDrawMatchesTex", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvDrawMatchesTex, "EmuCore/GS", "StereoRejectFmvDrawMatchesTex", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvNoShuffle, "EmuCore/GS", "StereoRequireFmvNoShuffle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvNoShuffle, "EmuCore/GS", "StereoRejectFmvNoShuffle", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvNoMipmap, "EmuCore/GS", "StereoRequireFmvNoMipmap", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvNoMipmap, "EmuCore/GS", "StereoRejectFmvNoMipmap", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvLinearSampling, "EmuCore/GS", "StereoRequireFmvLinearSampling", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvLinearSampling, "EmuCore/GS", "StereoRejectFmvLinearSampling", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvEeUpload, "EmuCore/GS", "StereoRequireFmvEeUpload", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvEeUpload, "EmuCore/GS", "StereoRejectFmvEeUpload", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvDisplayMatch, "EmuCore/GS", "StereoRequireFmvDisplayMatch", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvDisplayMatch, "EmuCore/GS", "StereoRejectFmvDisplayMatch", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvRecentEeUpload, "EmuCore/GS", "StereoRequireFmvRecentEeUpload", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvRecentEeUpload, "EmuCore/GS", "StereoRejectFmvRecentEeUpload", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRequireFmvRecentTransferDraw, "EmuCore/GS", "StereoRequireFmvRecentTransferDraw", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_hw.stereoRejectFmvRecentTransferDraw, "EmuCore/GS", "StereoRejectFmvRecentTransferDraw", false);
	connect(m_hw.upscaleMultiplier, &QComboBox::currentIndexChanged, this,
		&GraphicsSettingsWidget::onUpscaleMultiplierChanged);
	connect(m_hw.trilinearFiltering, &QComboBox::currentIndexChanged, this,
		&GraphicsSettingsWidget::onTrilinearFilteringChanged);
	connect(m_hw.stereoscopicMode, &QComboBox::currentIndexChanged, this, &GraphicsSettingsWidget::onStereoscopicModeChanged);
	onTrilinearFilteringChanged();
	onStereoscopicModeChanged();

	//////////////////////////////////////////////////////////////////////////
	// SW Settings
	//////////////////////////////////////////////////////////////////////////
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_sw.swTextureFiltering, "EmuCore/GS", "filter", static_cast<int>(BiFiltering::PS2));
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_sw.extraSWThreads, "EmuCore/GS", "extrathreads", 2);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_sw.swAutoFlush, "EmuCore/GS", "autoflush_sw", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_sw.swMipmap, "EmuCore/GS", "mipmap", true);

	//////////////////////////////////////////////////////////////////////////
	// HW Renderer Fixes
	//////////////////////////////////////////////////////////////////////////
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_fixes.cpuSpriteRenderBW, "EmuCore/GS", "UserHacks_CPUSpriteRenderBW", 0);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_fixes.cpuSpriteRenderLevel, "EmuCore/GS", "UserHacks_CPUSpriteRenderLevel", 0);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_fixes.cpuCLUTRender, "EmuCore/GS", "UserHacks_CPUCLUTRender", 0);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_fixes.gpuTargetCLUTMode, "EmuCore/GS", "UserHacks_GPUTargetCLUTMode", 0);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_fixes.skipDrawStart, "EmuCore/GS", "UserHacks_SkipDraw_Start", 0);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_fixes.skipDrawEnd, "EmuCore/GS", "UserHacks_SkipDraw_End", 0);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_fixes.hwAutoFlush, "EmuCore/GS", "UserHacks_AutoFlushLevel", 0);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_fixes.frameBufferConversion, "EmuCore/GS", "UserHacks_CPU_FB_Conversion", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_fixes.disableDepthEmulation, "EmuCore/GS", "UserHacks_DisableDepthSupport", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_fixes.disableSafeFeatures, "EmuCore/GS", "UserHacks_Disable_Safe_Features", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_fixes.disableRenderFixes, "EmuCore/GS", "UserHacks_DisableRenderFixes", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_fixes.preloadFrameData, "EmuCore/GS", "preload_frame_with_gs_data", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(
		sif, m_fixes.disablePartialInvalidation, "EmuCore/GS", "UserHacks_DisablePartialInvalidation", false);
	SettingWidgetBinder::BindWidgetToIntSetting(
		sif, m_fixes.textureInsideRt, "EmuCore/GS", "UserHacks_TextureInsideRt", static_cast<int>(GSTextureInRtMode::Disabled));
	SettingWidgetBinder::BindWidgetToIntSetting(
		sif, m_fixes.limit24BitDepth, "EmuCore/GS", "UserHacks_Limit24BitDepth", static_cast<int>(GSLimit24BitDepth::Disabled));
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_fixes.readTCOnClose, "EmuCore/GS", "UserHacks_ReadTCOnClose", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_fixes.estimateTextureRegion, "EmuCore/GS", "UserHacks_EstimateTextureRegion", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_fixes.gpuPaletteConversion, "EmuCore/GS", "paltex", false);
	connect(m_fixes.cpuSpriteRenderBW, &QComboBox::currentIndexChanged, this,
		&GraphicsSettingsWidget::onCPUSpriteRenderBWChanged);
	connect(m_fixes.gpuPaletteConversion, &QCheckBox::checkStateChanged, this, &GraphicsSettingsWidget::onGpuPaletteConversionChanged);
	onCPUSpriteRenderBWChanged();
	onGpuPaletteConversionChanged(m_fixes.gpuPaletteConversion->checkState());

	//////////////////////////////////////////////////////////////////////////
	// HW Upscaling Fixes
	//////////////////////////////////////////////////////////////////////////
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_upscaling.halfPixelOffset, "EmuCore/GS", "UserHacks_HalfPixelOffset", 0);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_upscaling.nativeScaling, "EmuCore/GS", "UserHacks_native_scaling", 0);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_upscaling.roundSprite, "EmuCore/GS", "UserHacks_round_sprite_offset", 0);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_upscaling.bilinearHack, "EmuCore/GS", "UserHacks_BilinearHack", 0);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_upscaling.textureOffsetX, "EmuCore/GS", "UserHacks_TCOffsetX", 0);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_upscaling.textureOffsetY, "EmuCore/GS", "UserHacks_TCOffsetY", 0);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_upscaling.alignSprite, "EmuCore/GS", "UserHacks_align_sprite_X", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_upscaling.mergeSprite, "EmuCore/GS", "UserHacks_merge_pp_sprite", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_upscaling.forceEvenSpritePosition, "EmuCore/GS", "UserHacks_forceEvenSpritePosition", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_upscaling.nativePaletteDraw, "EmuCore/GS", "UserHacks_NativePaletteDraw", false);

	//////////////////////////////////////////////////////////////////////////
	// Texture Replacements
	//////////////////////////////////////////////////////////////////////////
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_texture.dumpReplaceableTextures, "EmuCore/GS", "DumpReplaceableTextures", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_texture.dumpReplaceableMipmaps, "EmuCore/GS", "DumpReplaceableMipmaps", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_texture.dumpTexturesWithFMVActive, "EmuCore/GS", "DumpTexturesWithFMVActive", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_texture.loadTextureReplacements, "EmuCore/GS", "LoadTextureReplacements", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(
		sif, m_texture.loadTextureReplacementsAsync, "EmuCore/GS", "LoadTextureReplacementsAsync", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_texture.precacheTextureReplacements, "EmuCore/GS", "PrecacheTextureReplacements", false);
	SettingWidgetBinder::BindWidgetToFolderSetting(sif, m_texture.texturesDirectory, m_texture.texturesBrowse, m_texture.texturesOpen, m_texture.texturesReset,
		"Folders", "Textures", Path::Combine(EmuFolders::DataRoot, "textures"));
	connect(m_texture.dumpReplaceableTextures, &QCheckBox::checkStateChanged, this, &GraphicsSettingsWidget::onTextureDumpChanged);
	connect(m_texture.loadTextureReplacements, &QCheckBox::checkStateChanged, this, &GraphicsSettingsWidget::onTextureReplacementChanged);
	onTextureDumpChanged();
	onTextureReplacementChanged();

	if (dialog()->isPerGameSettings())
	{
		m_texture.verticalLayout->removeWidget(m_texture.texturesDirectoryBox);
		m_texture.texturesDirectoryBox->deleteLater();
		m_texture.texturesDirectoryBox = nullptr;
		m_texture.texturesDirectory = nullptr;
		m_texture.texturesBrowse = nullptr;
		m_texture.texturesOpen = nullptr;
		m_texture.texturesReset = nullptr;
		m_texture.textureDescriptionText = nullptr;
	}

	//////////////////////////////////////////////////////////////////////////
	// Post-Processing Settings
	//////////////////////////////////////////////////////////////////////////
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_post.fxaa, "EmuCore/GS", "fxaa", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_post.shadeBoost, "EmuCore/GS", "ShadeBoost", false);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_post.shadeBoostBrightness, "EmuCore/GS", "ShadeBoost_Brightness", Pcsx2Config::GSOptions::DEFAULT_SHADEBOOST_BRIGHTNESS);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_post.shadeBoostContrast, "EmuCore/GS", "ShadeBoost_Contrast", Pcsx2Config::GSOptions::DEFAULT_SHADEBOOST_CONTRAST);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_post.shadeBoostGamma, "EmuCore/GS", "ShadeBoost_Gamma", Pcsx2Config::GSOptions::DEFAULT_SHADEBOOST_GAMMA);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_post.shadeBoostSaturation, "EmuCore/GS", "ShadeBoost_Saturation", Pcsx2Config::GSOptions::DEFAULT_SHADEBOOST_SATURATION);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_post.tvShader, "EmuCore/GS", "TVShader", DEFAULT_TV_SHADER_MODE);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_post.casMode, "EmuCore/GS", "CASMode", static_cast<int>(GSCASMode::Disabled));
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_post.casSharpness, "EmuCore/GS", "CASSharpness", DEFAULT_CAS_SHARPNESS);

	connect(m_post.shadeBoost, &QCheckBox::checkStateChanged, this, &GraphicsSettingsWidget::onShadeBoostChanged);
	onShadeBoostChanged();
	connect(m_osd.messagesPos, &QComboBox::currentIndexChanged, this, &GraphicsSettingsWidget::onMessagesPosChanged);
	connect(m_osd.performancePos, &QComboBox::currentIndexChanged, this, &GraphicsSettingsWidget::onPerformancePosChanged);
	onMessagesPosChanged();
	onPerformancePosChanged();

	//////////////////////////////////////////////////////////////////////////
	// OSD Settings
	//////////////////////////////////////////////////////////////////////////
	SettingWidgetBinder::BindWidgetToFloatSetting(sif, m_osd.scale, "EmuCore/GS", "OsdScale", 100.0f);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_osd.messagesPos, "EmuCore/GS", "OsdMessagesPos", static_cast<int>(OsdOverlayPos::TopLeft));
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_osd.performancePos, "EmuCore/GS", "OsdPerformancePos", static_cast<int>(OsdOverlayPos::TopRight));
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showSpeedPercentages, "EmuCore/GS", "OsdShowSpeed", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showFPS, "EmuCore/GS", "OsdShowFPS", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showVPS, "EmuCore/GS", "OsdShowVPS", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showResolution, "EmuCore/GS", "OsdShowResolution", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showGSStats, "EmuCore/GS", "OsdShowGSStats", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showUsageCPU, "EmuCore/GS", "OsdShowCPU", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showUsageGPU, "EmuCore/GS", "OsdShowGPU", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showStatusIndicators, "EmuCore/GS", "OsdShowIndicators", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showFrameTimes, "EmuCore/GS", "OsdShowFrameTimes", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showHardwareInfo, "EmuCore/GS", "OsdShowHardwareInfo", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showVersion, "EmuCore/GS", "OsdShowVersion", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showSettings, "EmuCore/GS", "OsdShowSettings", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showPatches, "EmuCore/GS", "OsdshowPatches", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showInputs, "EmuCore/GS", "OsdShowInputs", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showVideoCapture, "EmuCore/GS", "OsdShowVideoCapture", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showInputRec, "EmuCore/GS", "OsdShowInputRec", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.showTextureReplacements, "EmuCore/GS", "OsdShowTextureReplacements", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_osd.warnAboutUnsafeSettings, "EmuCore", "OsdWarnAboutUnsafeSettings", true);

	//////////////////////////////////////////////////////////////////////////
	// Advanced Settings
	//////////////////////////////////////////////////////////////////////////
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_advanced.useBlitSwapChain, "EmuCore/GS", "UseBlitSwapChain", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_advanced.useDebugDevice, "EmuCore/GS", "UseDebugDevice", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_advanced.disableMailboxPresentation, "EmuCore/GS", "DisableMailboxPresentation", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_advanced.extendedUpscales, "EmuCore/GS", "ExtendedUpscalingMultipliers", false);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_advanced.exclusiveFullscreenControl, "EmuCore/GS", "ExclusiveFullscreenControl", -1, -1);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_advanced.overrideTextureBarriers, "EmuCore/GS", "OverrideTextureBarriers", -1, -1);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_advanced.gsDumpCompression, "EmuCore/GS", "GSDumpCompression", static_cast<int>(GSDumpCompressionMethod::Zstandard));
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_advanced.disableFramebufferFetch, "EmuCore/GS", "DisableFramebufferFetch", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_advanced.disableShaderCache, "EmuCore/GS", "DisableShaderCache", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_advanced.disableVertexShaderExpand, "EmuCore/GS", "DisableVertexShaderExpand", false);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_advanced.gsDownloadMode, "EmuCore/GS", "HWDownloadMode", static_cast<int>(GSHardwareDownloadMode::Enabled));
	SettingWidgetBinder::BindWidgetToFloatSetting(sif, m_advanced.ntscFrameRate, "EmuCore/GS", "FrameRateNTSC", 59.94f);
	SettingWidgetBinder::BindWidgetToFloatSetting(sif, m_advanced.palFrameRate, "EmuCore/GS", "FrameRatePAL", 50.00f);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_advanced.spinCPUDuringReadbacks, "EmuCore/GS", "HWSpinCPUForReadbacks", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_advanced.spinGPUDuringReadbacks, "EmuCore/GS", "HWSpinGPUForReadbacks", false);
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_advanced.texturePreloading, "EmuCore/GS", "texture_preloading", static_cast<int>(TexturePreloadingLevel::Off));

	setTabVisible(m_advanced_tab, QtHost::ShouldShowAdvancedSettings());

	//////////////////////////////////////////////////////////////////////////
	// Non-trivial settings
	//////////////////////////////////////////////////////////////////////////
	const int renderer = dialog()->getEffectiveIntValue("EmuCore/GS", "Renderer", static_cast<int>(GSRendererType::Auto));
	for (const RendererInfo& ri : s_renderer_info)
	{
		m_header.rendererDropdown->addItem(qApp->translate("GraphicsSettingsWidget", ri.name));
		if (renderer == static_cast<int>(ri.type))
			m_header.rendererDropdown->setCurrentIndex(m_header.rendererDropdown->count() - 1);
	}

	// per-game override for renderer is slightly annoying, since we need to populate the global setting field
	if (sif)
	{
		const int global_renderer = Host::GetBaseIntSettingValue("EmuCore/GS", "Renderer", static_cast<int>(GSRendererType::Auto));
		QString global_renderer_name;
		for (const RendererInfo& ri : s_renderer_info)
		{
			if (global_renderer == static_cast<int>(ri.type))
				global_renderer_name = qApp->translate("GraphicsSettingsWidget", ri.name);
		}
		m_header.rendererDropdown->insertItem(0, tr("Use Global Setting [%1]").arg(global_renderer_name));

		// Effective Index already selected, set to global if setting is not per-game
		int override_renderer;
		if (!sif->GetIntValue("EmuCore/GS", "Renderer", &override_renderer))
			m_header.rendererDropdown->setCurrentIndex(0);
	}

	connect(m_header.rendererDropdown, &QComboBox::currentIndexChanged, this, &GraphicsSettingsWidget::onRendererChanged);
	connect(m_header.adapterDropdown, &QComboBox::currentIndexChanged, this, &GraphicsSettingsWidget::onAdapterChanged);
	connect(m_hw.enableHWFixes, &QCheckBox::checkStateChanged, this, &GraphicsSettingsWidget::updateRendererDependentOptions);
	connect(m_advanced.extendedUpscales, &QCheckBox::checkStateChanged, this, &GraphicsSettingsWidget::updateRendererDependentOptions);
	connect(m_hw.textureFiltering, &QComboBox::currentIndexChanged, this, &GraphicsSettingsWidget::onTextureFilteringChange);
	connect(m_sw.swTextureFiltering, &QComboBox::currentIndexChanged, this, &GraphicsSettingsWidget::onSWTextureFilteringChange);
	updateRendererDependentOptions();

#ifndef _WIN32
	// Exclusive fullscreen control is Windows-only.
	m_advanced.advancedOptionsFormLayout->removeRow(2);
	m_advanced.exclusiveFullscreenControl = nullptr;
#endif

#ifndef PCSX2_DEVBUILD
	if (!dialog()->isPerGameSettings())
	{
		// Only allow disabling readbacks for per-game settings, it's too dangerous.
		m_advanced.advancedOptionsFormLayout->removeRow(0);
		m_advanced.gsDownloadMode = nullptr;

		// Don't allow setting hardware fixes globally.
		// Too many stupid YouTube "best settings" guides that break other games.
		m_hw.hardwareRenderingOptionsLayout->removeWidget(m_hw.enableHWFixes);
		delete m_hw.enableHWFixes;
		m_hw.enableHWFixes = nullptr;
	}
#endif

	// Get rid of widescreen/no-interlace checkboxes from per-game settings, and migrate them to Patches if necessary.
	if (dialog()->isPerGameSettings())
	{
		SettingsInterface* si = dialog()->getSettingsInterface();
		bool needs_save = false;

		if (si->ContainsValue("EmuCore", "EnableWideScreenPatches"))
		{
			const bool ws_enabled = si->GetBoolValue("EmuCore", "EnableWideScreenPatches");
			si->DeleteValue("EmuCore", "EnableWideScreenPatches");

			const char* WS_PATCH_NAME = "Widescreen 16:9";
			if (ws_enabled)
			{
				si->AddToStringList(Patch::PATCHES_CONFIG_SECTION, Patch::PATCH_ENABLE_CONFIG_KEY, WS_PATCH_NAME);
				si->RemoveFromStringList(Patch::PATCHES_CONFIG_SECTION, Patch::PATCH_DISABLE_CONFIG_KEY, WS_PATCH_NAME);
			}
			else
			{
				si->AddToStringList(Patch::PATCHES_CONFIG_SECTION, Patch::PATCH_DISABLE_CONFIG_KEY, WS_PATCH_NAME);
				si->RemoveFromStringList(Patch::PATCHES_CONFIG_SECTION, Patch::PATCH_ENABLE_CONFIG_KEY, WS_PATCH_NAME);
			}
			needs_save = true;
		}

		if (si->ContainsValue("EmuCore", "EnableNoInterlacingPatches"))
		{
			const bool ni_enabled = si->GetBoolValue("EmuCore", "EnableNoInterlacingPatches");
			si->DeleteValue("EmuCore", "EnableNoInterlacingPatches");

			const char* NI_PATCH_NAME = "No-Interlacing";
			if (ni_enabled)
			{
				si->AddToStringList(Patch::PATCHES_CONFIG_SECTION, Patch::PATCH_ENABLE_CONFIG_KEY, NI_PATCH_NAME);
				si->RemoveFromStringList(Patch::PATCHES_CONFIG_SECTION, Patch::PATCH_DISABLE_CONFIG_KEY, NI_PATCH_NAME);
			}
			else
			{
				si->AddToStringList(Patch::PATCHES_CONFIG_SECTION, Patch::PATCH_DISABLE_CONFIG_KEY, NI_PATCH_NAME);
				si->RemoveFromStringList(Patch::PATCHES_CONFIG_SECTION, Patch::PATCH_ENABLE_CONFIG_KEY, NI_PATCH_NAME);
			}
			needs_save = true;
		}

		if (needs_save)
		{
			dialog()->saveAndReloadGameSettings();
		}

		m_display.displayGridLayout->removeWidget(m_display.widescreenPatches);
		m_display.displayGridLayout->removeWidget(m_display.noInterlacingPatches);
		m_display.widescreenPatches->deleteLater();
		m_display.noInterlacingPatches->deleteLater();
		m_display.widescreenPatches = nullptr;
		m_display.noInterlacingPatches = nullptr;
	}

	// Capture settings
	{
		for (const char** container = Pcsx2Config::GSOptions::CaptureContainers; *container; container++)
		{
			const QString name(QString::fromUtf8(*container));
			m_capture.captureContainer->addItem(name.toUpper(), name);
		}

		SettingWidgetBinder::BindWidgetToStringSetting(sif, m_capture.captureContainer, "EmuCore/GS", "CaptureContainer");
		connect(m_capture.captureContainer, &QComboBox::currentIndexChanged, this, &GraphicsSettingsWidget::onCaptureContainerChanged);

		SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_capture.enableVideoCapture, "EmuCore/GS", "EnableVideoCapture", true);
		SettingWidgetBinder::BindWidgetToIntSetting(
			sif, m_capture.videoCaptureBitrate, "EmuCore/GS", "VideoCaptureBitrate", Pcsx2Config::GSOptions::DEFAULT_VIDEO_CAPTURE_BITRATE);
		SettingWidgetBinder::BindWidgetToIntSetting(
			sif, m_capture.videoCaptureWidth, "EmuCore/GS", "VideoCaptureWidth", Pcsx2Config::GSOptions::DEFAULT_VIDEO_CAPTURE_WIDTH);
		SettingWidgetBinder::BindWidgetToIntSetting(
			sif, m_capture.videoCaptureHeight, "EmuCore/GS", "VideoCaptureHeight", Pcsx2Config::GSOptions::DEFAULT_VIDEO_CAPTURE_HEIGHT);
		SettingWidgetBinder::BindWidgetToBoolSetting(
			sif, m_capture.videoCaptureResolutionAuto, "EmuCore/GS", "VideoCaptureAutoResolution", true);
		SettingWidgetBinder::BindWidgetToBoolSetting(
			sif, m_capture.enableVideoCaptureArguments, "EmuCore/GS", "EnableVideoCaptureParameters", false);
		SettingWidgetBinder::BindWidgetToStringSetting(sif, m_capture.videoCaptureArguments, "EmuCore/GS", "VideoCaptureParameters");
		SettingWidgetBinder::BindWidgetToIntSetting(
			sif, m_capture.screenshotQuality, "EmuCore/GS", "ScreenshotQuality", 90);
		connect(m_capture.enableVideoCapture, &QCheckBox::checkStateChanged, this, &GraphicsSettingsWidget::onEnableVideoCaptureChanged);
		connect(
			m_capture.videoCaptureResolutionAuto, &QCheckBox::checkStateChanged, this, &GraphicsSettingsWidget::onVideoCaptureAutoResolutionChanged);
		connect(m_capture.enableVideoCaptureArguments, &QCheckBox::checkStateChanged, this,
			&GraphicsSettingsWidget::onEnableVideoCaptureArgumentsChanged);

		SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_capture.enableAudioCapture, "EmuCore/GS", "EnableAudioCapture", true);
		SettingWidgetBinder::BindWidgetToIntSetting(
			sif, m_capture.audioCaptureBitrate, "EmuCore/GS", "AudioCaptureBitrate", Pcsx2Config::GSOptions::DEFAULT_AUDIO_CAPTURE_BITRATE);
		SettingWidgetBinder::BindWidgetToBoolSetting(
			sif, m_capture.enableAudioCaptureArguments, "EmuCore/GS", "EnableAudioCaptureParameters", false);
		SettingWidgetBinder::BindWidgetToStringSetting(sif, m_capture.audioCaptureArguments, "EmuCore/GS", "AudioCaptureParameters");
		connect(m_capture.enableAudioCapture, &QCheckBox::checkStateChanged, this, &GraphicsSettingsWidget::onEnableAudioCaptureChanged);
		connect(m_capture.enableAudioCaptureArguments, &QCheckBox::checkStateChanged, this,
			&GraphicsSettingsWidget::onEnableAudioCaptureArgumentsChanged);

		onCaptureContainerChanged();
		onCaptureCodecChanged();
		onEnableVideoCaptureChanged();
		onEnableVideoCaptureArgumentsChanged();
		onVideoCaptureAutoResolutionChanged();
		onEnableAudioCaptureChanged();
		onEnableAudioCaptureArgumentsChanged();
	}

	// Display tab
	{
		dialog()->registerWidgetHelp(m_display.widescreenPatches, tr("Enable Widescreen Patches"), tr("Unchecked"),
			tr("Automatically loads and applies widescreen patches on game start. Can cause issues."));

		dialog()->registerWidgetHelp(m_display.noInterlacingPatches, tr("Enable No-Interlacing Patches"), tr("Unchecked"),
			tr("Automatically loads and applies no-interlacing patches on game start. Can cause issues."));

		dialog()->registerWidgetHelp(m_display.disableInterlaceOffset, tr("Disable Interlace Offset"), tr("Unchecked"),
			tr("Disables interlacing offset which may reduce blurring in some situations."));

		dialog()->registerWidgetHelp(m_display.bilinearFiltering, tr("Bilinear Filtering"), tr("Bilinear (Smooth)"),
			tr("Enables bilinear post processing filter. Smooths the overall picture as it is displayed on the screen. Corrects "
			   "positioning between pixels."));

		dialog()->registerWidgetHelp(m_display.PCRTCOffsets, tr("Screen Offsets"), tr("Unchecked"),
			//: PCRTC: Programmable CRT (Cathode Ray Tube) Controller.
			tr("Enables PCRTC Offsets which position the screen as the game requests. Useful for some games such as WipEout Fusion for its "
			   "screen shake effect, but can make the picture blurry."));

		dialog()->registerWidgetHelp(m_display.PCRTCOverscan, tr("Show Overscan"), tr("Unchecked"),
			tr("Enables the option to show the overscan area on games which draw more than the safe area of the screen."));

		dialog()->registerWidgetHelp(
			m_display.fmvAspectRatio, tr("FMV Aspect Ratio Override"), tr("Off (Default)"),
			tr("Overrides the full-motion video (FMV) aspect ratio. "
			   "If disabled, the FMV Aspect Ratio will match the same value as the general Aspect Ratio setting."));

		dialog()->registerWidgetHelp(m_display.PCRTCAntiBlur, tr("Anti-Blur"), tr("Checked"),
			tr("Enables internal Anti-Blur hacks. Less accurate than PS2 rendering but will make a lot of games look less blurry."));

		dialog()->registerWidgetHelp(m_display.integerScaling, tr("Integer Scaling"), tr("Unchecked"),
			tr("Adds padding to the display area to ensure that the ratio between pixels on the host to pixels in the console is an "
			   "integer number. May result in a sharper image in some 2D games."));

		dialog()->registerWidgetHelp(m_display.aspectRatio, tr("Aspect Ratio"), tr("Auto Standard (4:3/3:2 Progressive)"),
			tr("Changes the aspect ratio used to display the console's output to the screen. The default is Auto Standard (4:3/3:2 "
			   "Progressive) which automatically adjusts the aspect ratio to match how a game would be shown on a typical TV of the era, and adapts to widescreen/ultrawide game patches."));

		dialog()->registerWidgetHelp(m_display.interlacing, tr("Deinterlacing"), tr("Automatic (Default)"), tr("Determines the deinterlacing method to be used on the interlaced screen of the emulated console. Automatic should be able to correctly deinterlace most games, but if you see visibly shaky graphics, try one of the other options."));

		dialog()->registerWidgetHelp(m_capture.screenshotSize, tr("Screenshot Resolution"), tr("Display Resolution"),
			tr("Determines the resolution at which screenshots will be saved. Internal resolutions preserve more detail at the cost of "
			   "file size."));

		dialog()->registerWidgetHelp(m_capture.screenshotFormat, tr("Screenshot Format"), tr("PNG"),
			tr("Selects the format which will be used to save screenshots. JPEG produces smaller files, but loses detail."));

		dialog()->registerWidgetHelp(m_capture.screenshotQuality, tr("Screenshot Quality"), tr("90%"),
			tr("Selects the quality at which screenshots will be compressed. Higher values preserve more detail for JPEG and WebP, and reduce file "
			   "size for PNG."));

		dialog()->registerWidgetHelp(m_display.stretchY, tr("Vertical Stretch"), tr("100%"),
			// Characters </> need to be converted into entities in order to be shown correctly.
			tr("Stretches (&lt; 100%) or squashes (&gt; 100%) the vertical component of the display."));

		dialog()->registerWidgetHelp(m_display.fullscreenModes, tr("Fullscreen Mode"), tr("Borderless Fullscreen"),
			tr("Chooses the fullscreen resolution and frequency."));

		dialog()->registerWidgetHelp(
			m_display.cropLeft, tr("Left"), tr("0px"), tr("Changes the number of pixels cropped from the left side of the display."));

		dialog()->registerWidgetHelp(
			m_display.cropTop, tr("Top"), tr("0px"), tr("Changes the number of pixels cropped from the top of the display."));

		dialog()->registerWidgetHelp(
			m_display.cropRight, tr("Right"), tr("0px"), tr("Changes the number of pixels cropped from the right side of the display."));

		dialog()->registerWidgetHelp(
			m_display.cropBottom, tr("Bottom"), tr("0px"), tr("Changes the number of pixels cropped from the bottom of the display."));
	}

	// Rendering tab
	{
		// Hardware
		dialog()->registerWidgetHelp(m_hw.upscaleMultiplier, tr("Internal Resolution"), tr("Native (PS2) (Default)"),
			tr("Control the resolution at which games are rendered. High resolutions can impact performance on "
			   "older or lower-end GPUs.<br>Non-native resolution may cause minor graphical issues in some games.<br>"
			   "FMV resolution will remain unchanged, as the video files are pre-rendered."));

		dialog()->registerWidgetHelp(
			m_hw.mipmapping, tr("Mipmapping"), tr("Checked"), tr("Enables mipmapping, which some games require to render correctly. Mipmapping uses progressively lower resolution variants of textures at progressively further distances to reduce processing load and avoid visual artifacts."));

		dialog()->registerWidgetHelp(
			m_hw.textureFiltering, tr("Texture Filtering"), tr("Bilinear (PS2)"),
			tr("Changes what filtering algorithm is used to map textures to surfaces.<br> "
			   "Nearest: Makes no attempt to blend colors.<br> "
			   "Bilinear (Forced): Will blend colors together to remove harsh edges between different colored pixels even if the game told the PS2 not to.<br> "
			   "Bilinear (PS2): Will apply filtering to all surfaces that a game instructs the PS2 to filter.<br> "
			   "Bilinear (Forced Excluding Sprites): Will apply filtering to all surfaces, even if the game told the PS2 not to, except sprites."));

		dialog()->registerWidgetHelp(m_hw.trilinearFiltering, tr("Trilinear Filtering"), tr("Automatic (Default)"),
			tr("Reduces blurriness of large textures applied to small, steeply angled surfaces by sampling colors from the two nearest Mipmaps. Requires Mipmapping to be 'on'.<br> "
			   "Off: Disables the feature.<br> "
			   "Trilinear (PS2): Applies Trilinear filtering to all surfaces that a game instructs the PS2 to.<br> "
			   "Trilinear (Forced): Applies Trilinear filtering to all surfaces, even if the game told the PS2 not to."));

		dialog()->registerWidgetHelp(m_hw.anisotropicFiltering, tr("Anisotropic Filtering"), tr("Off (Default)"),
			tr("Reduces texture aliasing at extreme viewing angles."));

		dialog()->registerWidgetHelp(m_hw.dithering, tr("Dithering"), tr("Unscaled (Default)"),
			tr("Reduces banding between colors and improves the perceived color depth.<br> "
			   "Off: Disables any dithering.<br> "
			   "Scaled: Upscaling-aware / Highest dithering effect.<br> "
			   "Unscaled: Native dithering / Lowest dithering effect, does not increase size of squares when upscaling.<br> "
			   "Force 32bit: Treats all draws as if they were 32bit to avoid banding and dithering."));

		dialog()->registerWidgetHelp(m_hw.blending, tr("Blending Accuracy"), tr("Basic (Recommended)"),
			tr("Control the accuracy level of the GS blending unit emulation.<br> "
			   "The higher the setting, the more blending is emulated in the shader accurately, and the higher the speed penalty will be."));

		dialog()->registerWidgetHelp(m_hw.stereoRequireDisplayBuffer1, tr("Require Display Buffer"), tr("Checked"),
			tr("Only apply stereoscopy to draws which match the active display framebuffer. Disabling this can include offscreen effects."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireDisplayBuffer2, tr("Require Display Buffer"), tr("Checked"),
        			tr("Only apply stereoscopy to draws which match the active display framebuffer. Disabling this can include offscreen effects."));
		dialog()->registerWidgetHelp(m_hw.stereoRequirePerspectiveUV, tr("Require Perspective UV"), tr("Checked"),
			tr("Require perspective-correct UVs for stereoscopic rendering. Useful for excluding flat UI draws."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireZVaries, tr("Require Varying Z"), tr("Checked"),
			tr("Require Z to vary within the draw before enabling stereoscopy."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireDepthActive, tr("Require Depth Test"), tr("Checked"),
			tr("Require depth test and writes to be active before enabling stereoscopy."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectSprites, tr("Reject Sprites"), tr("Checked"),
			tr("Disable stereoscopy for sprite/rect draws, which are commonly used for UI and 2D effects."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectUiLike, tr("Reject UI-like Sprites"), tr("Checked"),
			tr("Exclude sprite draws that look like UI (fixed UV, constant Q/Z, no depth)."));
		dialog()->registerWidgetHelp(m_hw.stereoUiSafeDetect, tr("UI Safe Detect"), tr("Unchecked"),
			tr("Safe UI detection mode for common overlays."));
		dialog()->registerWidgetHelp(m_hw.stereoUiAdvancedDetect, tr("UI Advanced Detect"), tr("Unchecked"),
			tr("Stricter UI detection mode for complex overlays."));
		dialog()->registerWidgetHelp(m_hw.stereoUiBackgroundDepth, tr("Background Depth"), tr("Unchecked"),
			tr("Only treat UI-like draws as UI when depth testing is active."));
		dialog()->registerWidgetHelp(m_hw.stereoMasterFix, tr("Master FIX"), tr("Unchecked"),
			tr("Master toggle for additional stereo fixes."));
		dialog()->registerWidgetHelp(m_hw.stereoMasterFixTest, tr("Master FIX Test"), tr("Unchecked"),
			tr("Experimental test toggle for stereo fixes."));
		dialog()->registerWidgetHelp(m_hw.stereoMasterFix1, tr("Master FIX 1"), tr("Unchecked"),
			tr("Additional stereo fix toggle."));
		dialog()->registerWidgetHelp(m_hw.stereoMasterFix2, tr("Master FIX 2"), tr("Unchecked"),
			tr("Additional stereo fix toggle."));
		dialog()->registerWidgetHelp(m_hw.stereoMasterFix3, tr("Master FIX 3"), tr("Unchecked"),
			tr("Additional stereo fix toggle."));
		dialog()->registerWidgetHelp(m_hw.stereoMasterFix4, tr("Master FIX 4"), tr("Unchecked"),
			tr("Additional stereo fix toggle."));
		dialog()->registerWidgetHelp(m_hw.stereoMasterFix5, tr("Master FIX 5"), tr("Unchecked"),
			tr("Additional stereo fix toggle."));
		dialog()->registerWidgetHelp(m_hw.stereoMasterFix6, tr("Master FIX 6"), tr("Unchecked"),
			tr("Additional stereo fix toggle."));
		dialog()->registerWidgetHelp(m_hw.stereoMasterFix7, tr("Master FIX 7"), tr("Unchecked"),
			tr("Additional stereo fix toggle."));
		dialog()->registerWidgetHelp(m_hw.stereoMasterFix8, tr("Master FIX 8"), tr("Unchecked"),
			tr("Additional stereo fix toggle."));
		dialog()->registerWidgetHelp(m_hw.stereoMasterFix9, tr("Master FIX 9"), tr("Unchecked"),
			tr("Additional stereo fix toggle."));
		dialog()->registerWidgetHelp(m_hw.stereoMasterFix10, tr("Master FIX 10"), tr("Unchecked"),
			tr("Additional stereo fix toggle."));
		dialog()->registerWidgetHelp(m_hw.stereoDominantEye, tr("Dominant Eye"), tr("No (recommended)"),
			tr("Biases stereo parallax toward the selected eye. Useful for FPS weapon alignment."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectNonPositiveZ, tr("Reject Z <= 0"), tr("Unchecked"),
			tr("Treat draws with non-positive Z as mono."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectSmallZRange, tr("Reject Small Z Range"), tr("Unchecked"),
			tr("Treat draws with a near-constant Z range as mono."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectSpriteBlit, tr("Reject Sprite Blit"), tr("Unchecked"),
			tr("Treat 1:1 sprite blits (UI-style) as mono."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectConstantColor, tr("Reject Constant Color"), tr("Unchecked"),
			tr("Treat constant-color draws as mono."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectScalingDraw, tr("Reject Scaling Draw"), tr("Unchecked"),
			tr("Disable stereoscopy for post-processing scaling draws."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectSbsInput, tr("Reject SBS Input"), tr("Unchecked"),
			tr("Disable stereoscopy when the source texture already looks like SBS."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectTabInput, tr("Reject TAB Input"), tr("Unchecked"),
			tr("Disable stereoscopy when the source texture already looks like TAB."));
		dialog()->registerWidgetHelp(m_hw.stereoUiDepth, tr("UI Depth"), tr("0.0"),
			tr("Depth offset applied to UI elements when stereoscopy is active. Negative values push UI back, positive values pull UI forward."));
		dialog()->registerWidgetHelp(m_hw.stereoUiSecondLayerDepth, tr("UI Second Layer Depth Offset"), tr("0.0"),
			tr("Additional depth offset applied when background depth detection is active."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireTextureMapping, tr("Require Texture Mapping"), tr("Unchecked"),
			tr("Only apply stereoscopy when texturing is enabled for the draw."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireAlphaBlend, tr("Require Alpha Blending"), tr("Unchecked"),
			tr("Only apply stereoscopy when alpha blending is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireAlphaTest, tr("Require Alpha Test"), tr("Unchecked"),
			tr("Only apply stereoscopy when alpha testing is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireUvVaries, tr("Require Varying UV"), tr("Unchecked"),
			tr("Only apply stereoscopy when UV coordinates vary across the draw."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireColorVaries, tr("Require Varying Color"), tr("Unchecked"),
			tr("Only apply stereoscopy when vertex colors vary across the draw."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFog, tr("Require Fog"), tr("Unchecked"),
			tr("Only apply stereoscopy when fog is enabled for the draw."));
		dialog()->registerWidgetHelp(m_hw.stereoStencilRequireDate, tr("Require DATE"), tr("Unchecked"),
			tr("Only apply stereoscopy when DATE is enabled for the draw."));
		dialog()->registerWidgetHelp(m_hw.stereoStencilRequireDatm, tr("Require DATM"), tr("Unchecked"),
			tr("Only apply stereoscopy when DATM is enabled for the draw."));
		dialog()->registerWidgetHelp(m_hw.stereoStencilRequireAte, tr("Require ATE"), tr("Unchecked"),
			tr("Only apply stereoscopy when alpha testing is enabled for the draw."));
		dialog()->registerWidgetHelp(m_hw.stereoStencilRequireAfailZbOnly, tr("Require AFAIL ZB Only"), tr("Unchecked"),
			tr("Only apply stereoscopy when alpha fail is set to ZB only."));
		dialog()->registerWidgetHelp(m_hw.stereoStencilRequireAfailNotKeep, tr("Require AFAIL Not Keep"), tr("Unchecked"),
			tr("Only apply stereoscopy when alpha fail does not keep existing values."));
		dialog()->registerWidgetHelp(m_hw.stereoStencilRequireZWrite, tr("Require Z Write"), tr("Unchecked"),
			tr("Only apply stereoscopy when Z writes are enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoStencilRequireZTest, tr("Require Z Test"), tr("Unchecked"),
			tr("Only apply stereoscopy when Z testing is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoStencilRequireZTestGequal, tr("Require Z Test GEQUAL"), tr("Unchecked"),
			tr("Only apply stereoscopy when Z test is set to GEQUAL."));
		dialog()->registerWidgetHelp(m_hw.stereoStencilRequireFbMask, tr("Require FB Mask"), tr("Unchecked"),
			tr("Only apply stereoscopy when a framebuffer mask is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoStencilRequireFbMaskFull, tr("Require Full FB Mask"), tr("Unchecked"),
			tr("Only apply stereoscopy when the framebuffer mask is fully enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoStencilRequireTexIsFb, tr("Require Texture Is FB"), tr("Unchecked"),
			tr("Only apply stereoscopy when the texture source matches the framebuffer."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFullscreenDraw, tr("Reject Fullscreen Draw Rect"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw rectangle covers the full render target."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFullscreenScissor, tr("Reject Fullscreen Scissor"), tr("Unchecked"),
			tr("Disable stereoscopy when the scissor matches the full render target."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFullCover, tr("Reject Full Cover"), tr("Unchecked"),
			tr("Disable stereoscopy when the primitive covers the target without gaps."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectSpriteNoGaps, tr("Reject Sprite No Gaps"), tr("Unchecked"),
			tr("Disable stereoscopy for sprite draws that cover without gaps."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectTexIsFb, tr("Reject Tex-is-FB"), tr("Unchecked"),
			tr("Disable stereoscopy when the shader samples from the framebuffer."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectChannelShuffle, tr("Reject Channel Shuffle"), tr("Unchecked"),
			tr("Disable stereoscopy for channel shuffle draws."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectTextureShuffle, tr("Reject Texture Shuffle"), tr("Unchecked"),
			tr("Disable stereoscopy for texture shuffle effects."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFullscreenShuffle, tr("Reject Fullscreen Shuffle"), tr("Unchecked"),
			tr("Disable stereoscopy when a fullscreen shuffle is detected."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectShaderShuffle, tr("Reject Shader Shuffle"), tr("Unchecked"),
			tr("Disable stereoscopy for shader-based shuffle paths."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectShuffleAcross, tr("Reject Shuffle Across"), tr("Unchecked"),
			tr("Disable stereoscopy for shuffle-across effects."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectShuffleSame, tr("Reject Shuffle Same"), tr("Unchecked"),
			tr("Disable stereoscopy for same-group shuffle effects."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectChannelFetch, tr("Reject Channel Fetch"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw uses channel fetching."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectChannelFetchFb, tr("Reject Channel Fetch FB"), tr("Unchecked"),
			tr("Disable stereoscopy when channel fetch reads from the framebuffer."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFeedbackLoop, tr("Reject Feedback Loop"), tr("Unchecked"),
			tr("Disable stereoscopy when a framebuffer feedback loop is detected."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectColclip, tr("Reject Colclip"), tr("Unchecked"),
			tr("Disable stereoscopy when color clipping is active."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectRtaCorrection, tr("Reject RTA Correction"), tr("Unchecked"),
			tr("Disable stereoscopy when render target alpha correction is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectRtaSourceCorrection, tr("Reject RTA Source Correction"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when RTA source correction is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectColclipHw, tr("Reject Colclip HW"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when colclip HW emulation is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectColclip, tr("Reject Colclip"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when colclip is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectBlendMix, tr("Reject Blend Mix"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when blend mix paths are active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectPabe, tr("Reject PABE"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when PABE is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectFbMask, tr("Reject FB Mask"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when framebuffer masking is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectTexIsFb, tr("Reject Tex-is-FB"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when sampling the framebuffer."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectNoColor, tr("Reject No Color Output"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy for depth-only draws."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectNoColor1, tr("Reject No Color Output 1"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when secondary color output is disabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectAemFmt, tr("Reject AEM Format"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when AEM format is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectPalFmt, tr("Reject PAL Format"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when PAL format is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectDstFmt, tr("Reject DST Format"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when destination format is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectDepthFmt, tr("Reject Depth Format"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when depth format is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectAem, tr("Reject AEM"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when AEM is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectFba, tr("Reject FBA"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when FBA is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectFog, tr("Reject Fog"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when fog is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectIip, tr("Reject IIP"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when IIP (Gouraud) is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectDate, tr("Reject DATE"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when DATE is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectAtst, tr("Reject ATST"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when ATST is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectAfail, tr("Reject AFAIL"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when AFAIL is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectFst, tr("Reject FST"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when FST is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectTfx, tr("Reject TFX"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when TFX is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectTcc, tr("Reject TCC"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when TCC is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectWms, tr("Reject WMS"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when WMS is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectWmt, tr("Reject WMT"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when WMT is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectAdjs, tr("Reject ADJS"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when ADJS is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectAdjt, tr("Reject ADJT"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when ADJT is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectLtf, tr("Reject LTF"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when LTF is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectShuffle, tr("Reject Shuffle"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when shuffle is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectShuffleSame, tr("Reject Shuffle Same"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when shuffle same is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectReal16Src, tr("Reject Real16 Src"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when real16 source is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectProcessBa, tr("Reject Process BA"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when process BA is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectProcessRg, tr("Reject Process RG"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when process RG is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectShuffleAcross, tr("Reject Shuffle Across"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when shuffle across is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectWriteRg, tr("Reject Write RG"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when write RG is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectBlendA, tr("Reject Blend A"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when blend A is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectBlendB, tr("Reject Blend B"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when blend B is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectBlendC, tr("Reject Blend C"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when blend C is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectBlendD, tr("Reject Blend D"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when blend D is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectFixedOneA, tr("Reject Fixed One A"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when fixed-one-A is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectBlendHw, tr("Reject Blend HW"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when blend HW is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectAMasked, tr("Reject A Masked"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when alpha is masked."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectRoundInv, tr("Reject Round Inv"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when round inversion is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectChannel, tr("Reject Channel"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when channel fetch is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectChannelFb, tr("Reject Channel FB"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when channel fetch from FB is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectDither, tr("Reject Dither"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when dither is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectDitherAdjust, tr("Reject Dither Adjust"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when dither adjust is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectZClamp, tr("Reject Z Clamp"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when Z clamp is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectZFloor, tr("Reject Z Floor"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when Z floor is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectTCOffsetHack, tr("Reject TC Offset Hack"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when TC offset hack is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectUrbanChaosHle, tr("Reject Urban Chaos HLE"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when Urban Chaos HLE path is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectTalesOfAbyssHle, tr("Reject Tales of Abyss HLE"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when Tales of Abyss HLE path is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectAutomaticLod, tr("Reject Automatic LOD"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when automatic LOD is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectManualLod, tr("Reject Manual LOD"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when manual LOD is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectPointSampler, tr("Reject Point Sampler"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when point sampling is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectRegionRect, tr("Reject Region Rect"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when region rect path is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRejectScanmask, tr("Reject Scanmask"), tr("Unchecked"),
			tr("Universal fix: disable stereoscopy when scanmask is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireRtaCorrection, tr("Require RTA Correction"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when RTA correction is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireRtaSourceCorrection, tr("Require RTA Source Correction"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when RTA source correction is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireColclipHw, tr("Require Colclip HW"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when colclip HW emulation is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireColclip, tr("Require Colclip"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when colclip is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireBlendMix, tr("Require Blend Mix"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when blend mix paths are active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequirePabe, tr("Require PABE"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when PABE is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireFbMask, tr("Require FB Mask"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when framebuffer masking is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireTexIsFb, tr("Require Tex-is-FB"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when sampling the framebuffer."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireNoColor, tr("Require No Color Output"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy for depth-only draws."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireNoColor1, tr("Require No Color Output 1"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when secondary color output is disabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireAemFmt, tr("Require AEM Format"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when AEM format is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequirePalFmt, tr("Require PAL Format"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when PAL format is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireDstFmt, tr("Require DST Format"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when destination format is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireDepthFmt, tr("Require Depth Format"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when depth format is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireAem, tr("Require AEM"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when AEM is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireFba, tr("Require FBA"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when FBA is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireFog, tr("Require Fog"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when fog is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireIip, tr("Require IIP"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when IIP (Gouraud) is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireDate, tr("Require DATE"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when DATE is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireAtst, tr("Require ATST"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when ATST is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireAfail, tr("Require AFAIL"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when AFAIL is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireFst, tr("Require FST"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when FST is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireTfx, tr("Require TFX"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when TFX is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireTcc, tr("Require TCC"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when TCC is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireWms, tr("Require WMS"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when WMS is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireWmt, tr("Require WMT"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when WMT is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireAdjs, tr("Require ADJS"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when ADJS is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireAdjt, tr("Require ADJT"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when ADJT is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireLtf, tr("Require LTF"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when LTF is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireShuffle, tr("Require Shuffle"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when shuffle is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireShuffleSame, tr("Require Shuffle Same"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when shuffle same is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireReal16Src, tr("Require Real16 Src"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when real16 source is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireProcessBa, tr("Require Process BA"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when process BA is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireProcessRg, tr("Require Process RG"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when process RG is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireShuffleAcross, tr("Require Shuffle Across"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when shuffle across is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireWriteRg, tr("Require Write RG"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when write RG is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireBlendA, tr("Require Blend A"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when blend A is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireBlendB, tr("Require Blend B"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when blend B is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireBlendC, tr("Require Blend C"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when blend C is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireBlendD, tr("Require Blend D"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when blend D is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireFixedOneA, tr("Require Fixed One A"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when fixed-one-A is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireBlendHw, tr("Require Blend HW"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when blend HW is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireAMasked, tr("Require A Masked"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when alpha is masked."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireRoundInv, tr("Require Round Inv"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when round inversion is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireChannel, tr("Require Channel"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when channel fetch is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireChannelFb, tr("Require Channel FB"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when channel fetch from FB is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireDither, tr("Require Dither"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when dither is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireDitherAdjust, tr("Require Dither Adjust"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when dither adjust is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireZClamp, tr("Require Z Clamp"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when Z clamp is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireZFloor, tr("Require Z Floor"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when Z floor is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireTCOffsetHack, tr("Require TC Offset Hack"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when TC offset hack is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireUrbanChaosHle, tr("Require Urban Chaos HLE"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when Urban Chaos HLE path is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireTalesOfAbyssHle, tr("Require Tales of Abyss HLE"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when Tales of Abyss HLE path is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireAutomaticLod, tr("Require Automatic LOD"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when automatic LOD is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireManualLod, tr("Require Manual LOD"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when manual LOD is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequirePointSampler, tr("Require Point Sampler"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when point sampling is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireRegionRect, tr("Require Region Rect"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when region rect path is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireScanmask, tr("Require Scanmask"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when scanmask is non-zero."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireAlphaBlend, tr("Require Alpha Blend"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when alpha blending is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireAlphaTest, tr("Require Alpha Test"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when alpha test is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireDatm, tr("Require DATM"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when DATM is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireZTest, tr("Require Z Test"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when Z testing is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireZWrite, tr("Require Z Write"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when Z writes are enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireZTestAlways, tr("Require Z Test Always"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when Z test is set to ALWAYS."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireZTestNever, tr("Require Z Test Never"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when Z test is set to NEVER."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireAa1, tr("Require AA1"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when AA1 is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireChannelShuffle, tr("Require Channel Shuffle"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when channel shuffle is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireTextureShuffle, tr("Require Texture Shuffle"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when texture shuffle is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireFullscreenShuffle, tr("Require Fullscreen Shuffle"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when fullscreen shuffle is active."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequirePoints, tr("Require Points"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when point primitives are used."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireLines, tr("Require Lines"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when line primitives are used."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireTriangles, tr("Require Triangles"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when triangle primitives are used."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireSprites, tr("Require Sprites"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when sprite primitives are used."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireFixedQ, tr("Require Fixed Q"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when Q is constant across the draw."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireFixedZ, tr("Require Fixed Z"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when Z is constant across the draw."));
		dialog()->registerWidgetHelp(m_hw.stereoUniversalRequireConstantColor, tr("Require Constant Color"), tr("Unchecked"),
			tr("Universal fix: only allow stereoscopy when color is constant across the draw."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectBlendMix, tr("Reject Blend Mix"), tr("Unchecked"),
			tr("Disable stereoscopy when blend mix paths are active."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectPabe, tr("Reject PABE"), tr("Unchecked"),
			tr("Disable stereoscopy when per-pixel alpha blend is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectDither, tr("Reject Dither"), tr("Unchecked"),
			tr("Disable stereoscopy when dithering is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectScanmask, tr("Reject Scanmask"), tr("Unchecked"),
			tr("Disable stereoscopy when scanmask is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectRegionRect, tr("Reject Region Rect"), tr("Unchecked"),
			tr("Disable stereoscopy when a region rectangle path is used."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectNoColorOutput, tr("Reject No Color Output"), tr("Unchecked"),
			tr("Disable stereoscopy for depth-only or no-color outputs."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectHleShuffle, tr("Reject HLE Shuffle"), tr("Unchecked"),
			tr("Disable stereoscopy for HLE shuffle effects."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectTCOffsetHack, tr("Reject TC Offset Hack"), tr("Unchecked"),
			tr("Disable stereoscopy when texture coordinate offsets are hacked."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectPoints, tr("Reject Points"), tr("Unchecked"),
			tr("Disable stereoscopy for point primitives."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectLines, tr("Reject Lines"), tr("Unchecked"),
			tr("Disable stereoscopy for line primitives."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFlatShading, tr("Reject Flat Shading"), tr("Unchecked"),
			tr("Disable stereoscopy when flat shading is used."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFst, tr("Reject Fixed ST"), tr("Unchecked"),
			tr("Disable stereoscopy when fixed texture coordinates are used (Gran Turismo 4, Need for Speed series)"));
		dialog()->registerWidgetHelp(m_hw.stereoEnableOptions, tr("Enable options"), tr("Unchecked"),
			tr("Enable additional stereoscopy options."));
		dialog()->registerWidgetHelp(m_hw.stereoRemoveFixedSt, tr("Remove Fixed ST"), tr("Unchecked"),
			tr("Force remove when fixed texture coordinates are used."));
		dialog()->registerWidgetHelp(m_hw.stereoFixStencilShadows, tr("Fix Stencil Shadows"), tr("Unchecked"),
			tr("Disable stereoscopy for stencil shadow passes to reduce post-processing artifacts (Tekken 5, Soul Calibur 3)"));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFixedQ, tr("Reject Fixed Q"), tr("Unchecked"),
			tr("Disable stereoscopy when Q is constant across the draw."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectAa1, tr("Reject AA1"), tr("Unchecked"),
			tr("Disable stereoscopy when AA1 is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectNoZTest, tr("Reject No Z Test"), tr("Unchecked"),
			tr("Disable stereoscopy when Z testing is disabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectNoZWrite, tr("Reject No Z Write"), tr("Unchecked"),
			tr("Disable stereoscopy when Z writes are masked."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectZTestAlways, tr("Reject Z Test Always"), tr("Unchecked"),
			tr("Disable stereoscopy when Z test is set to ALWAYS."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectZTestNever, tr("Reject Z Test Never"), tr("Unchecked"),
			tr("Disable stereoscopy when Z test is set to NEVER."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectAlphaTestOff, tr("Reject Alpha Test Off"), tr("Unchecked"),
			tr("Disable stereoscopy when alpha test is disabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectAlphaTestAlways, tr("Reject Alpha Test Always"), tr("Unchecked"),
			tr("Disable stereoscopy when alpha test is set to ALWAYS."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectAlphaTestNever, tr("Reject Alpha Test Never"), tr("Unchecked"),
			tr("Disable stereoscopy when alpha test is set to NEVER."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectTfxModulate, tr("Reject TFX Modulate"), tr("Unchecked"),
			tr("Disable stereoscopy when TFX is MODULATE."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectTfxDecal, tr("Reject TFX Decal"), tr("Unchecked"),
			tr("Disable stereoscopy when TFX is DECAL (Tekken 5)"));
		dialog()->registerWidgetHelp(m_hw.stereoRejectTfxHighlight, tr("Reject TFX Highlight"), tr("Unchecked"),
			tr("Disable stereoscopy when TFX is HIGHLIGHT."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectTfxHighlight2, tr("Reject TFX Highlight2"), tr("Unchecked"),
			tr("Disable stereoscopy when TFX is HIGHLIGHT2."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectSmallDrawArea, tr("Reject Small Draw Area"), tr("Unchecked"),
			tr("Disable stereoscopy for small draw rectangles (useful for overlay debugging)."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectWideDrawBand, tr("Reject Wide Draw Band"), tr("Unchecked"),
			tr("Disable stereoscopy for wide, short draw bands (mirror-style overlays)."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectTopDrawBand, tr("Reject Top Draw Band"), tr("Unchecked"),
			tr("Disable stereoscopy for top-of-screen draw bands."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectRtSpriteNoDepth, tr("Reject RT Sprite (No Z Test)"), tr("Unchecked"),
			tr("Disable stereoscopy for RT-backed sprite draws without Z testing."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectRtSpriteAlphaBlend, tr("Reject RT Sprite (Alpha Blend)"), tr("Unchecked"),
			tr("Disable stereoscopy for RT-backed sprite draws using alpha blending."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireProcessTexture, tr("Require Process Texture"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw actually processes a texture."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectProcessTexture, tr("Reject Process Texture"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw processes a texture."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireSourceFromTarget, tr("Require Texture From Target"), tr("Unchecked"),
			tr("Disable stereoscopy unless the source texture comes from a render target."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectSourceFromTarget, tr("Reject Texture From Target"), tr("Unchecked"),
			tr("Disable stereoscopy when the source texture comes from a render target."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireDrawUsesTarget, tr("Require Draw Uses Target"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw samples from the current target."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectDrawUsesTarget, tr("Reject Draw Uses Target"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw samples from the current target."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireTexIsRt, tr("Require Texture Is RT"), tr("Unchecked"),
			tr("Disable stereoscopy unless the texture overlaps the render target."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectTexIsRt, tr("Reject Texture Is RT"), tr("Unchecked"),
			tr("Disable stereoscopy when the texture overlaps the render target."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireInTargetDraw, tr("Require In-Target Draw"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw is offset inside a target."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectInTargetDraw, tr("Reject In-Target Draw"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw is offset inside a target."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireTempZ, tr("Require Temp Z"), tr("Unchecked"),
			tr("Disable stereoscopy unless a temporary Z buffer is used."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectTempZ, tr("Reject Temp Z"), tr("Unchecked"),
			tr("Disable stereoscopy when a temporary Z buffer is used."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireOneBarrier, tr("Require One Barrier"), tr("Unchecked"),
			tr("Disable stereoscopy unless a single barrier is required."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectOneBarrier, tr("Reject One Barrier"), tr("Unchecked"),
			tr("Disable stereoscopy when a single barrier is required."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFullBarrier, tr("Require Full Barrier"), tr("Unchecked"),
			tr("Disable stereoscopy unless a full barrier is required."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFullBarrier, tr("Reject Full Barrier"), tr("Unchecked"),
			tr("Disable stereoscopy when a full barrier is required."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireSinglePass, tr("Require Single Pass"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw is single-pass."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectSinglePass, tr("Reject Single Pass"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw is single-pass."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFullscreenDrawArea, tr("Require Full Draw Area"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw area is fullscreen."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFullscreenDrawArea, tr("Reject Full Draw Area"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw area is fullscreen."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFullscreenSprite, tr("Require Fullscreen Sprite"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw is a fullscreen sprite."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFullscreenSprite, tr("Reject Fullscreen Sprite"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw is a fullscreen sprite."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireTexturedSprite, tr("Require Textured Sprite"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw is a textured sprite."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectTexturedSprite, tr("Reject Textured Sprite"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw is a textured sprite."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireRtOutput, tr("Require RT Output"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw writes to the render target."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectRtOutput, tr("Reject RT Output"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw writes to the render target."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireDepthOutput, tr("Require Depth Output"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw writes depth."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectDepthOutput, tr("Reject Depth Output"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw writes depth."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireDepthRead, tr("Require Depth Read"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw reads depth."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectDepthRead, tr("Reject Depth Read"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw reads depth."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireDepthWrite, tr("Require Depth Write"), tr("Unchecked"),
			tr("Disable stereoscopy unless depth writes are effective."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectDepthWrite, tr("Reject Depth Write"), tr("Unchecked"),
			tr("Disable stereoscopy when depth writes are effective."));
		dialog()->registerWidgetHelp(m_hw.stereoRequirePalettedTexture, tr("Require Paletted Texture"), tr("Unchecked"),
			tr("Disable stereoscopy unless a paletted texture is used."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectPalettedTexture, tr("Reject Paletted Texture"), tr("Unchecked"),
			tr("Disable stereoscopy when a paletted texture is used."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireDepthTexture, tr("Require Depth Texture"), tr("Unchecked"),
			tr("Disable stereoscopy unless a depth texture is used."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectDepthTexture, tr("Reject Depth Texture"), tr("Unchecked"),
			tr("Disable stereoscopy when a depth texture is used."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireMipmap, tr("Require Mipmap"), tr("Unchecked"),
			tr("Disable stereoscopy unless mipmapping is active."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectMipmap, tr("Reject Mipmap"), tr("Unchecked"),
			tr("Disable stereoscopy when mipmapping is active."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireLinearSampling, tr("Require Linear Sampling"), tr("Unchecked"),
			tr("Disable stereoscopy unless linear filtering is used."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectLinearSampling, tr("Reject Linear Sampling"), tr("Unchecked"),
			tr("Disable stereoscopy when linear filtering is used."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvActive, tr("Require IPU FMV Active"), tr("Unchecked"),
			tr("Disable stereoscopy unless the IPU FMV flag is active."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvActive, tr("Reject IPU FMV Active"), tr("Unchecked"),
			tr("Disable stereoscopy when the IPU FMV flag is active."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvHeuristic, tr("Require Heuristic Match"), tr("Unchecked"),
			tr("Disable stereoscopy unless the FMV heuristic detects a movie-style draw."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvHeuristic, tr("Reject Heuristic Match"), tr("Unchecked"),
			tr("Disable stereoscopy when the FMV heuristic detects a movie-style draw."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvSprite, tr("Require Sprite Primitive"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw is a sprite primitive."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvSprite, tr("Reject Sprite Primitive"), tr("Unchecked"),
			tr("Disable stereoscopy for sprite primitives."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvSingleSprite, tr("Require Single Sprite"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw is a single sprite (two vertices)."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvSingleSprite, tr("Reject Single Sprite"), tr("Unchecked"),
			tr("Disable stereoscopy for single-sprite draws."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvTextureMapping, tr("Require Texture Mapping"), tr("Unchecked"),
			tr("Disable stereoscopy unless texture mapping is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvTextureMapping, tr("Reject Texture Mapping"), tr("Unchecked"),
			tr("Disable stereoscopy when texture mapping is enabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvProcessTexture, tr("Require Process Texture"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw processes a texture."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvProcessTexture, tr("Reject Process Texture"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw processes a texture."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvFullscreenDrawArea, tr("Require Full Draw Area"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw area is fullscreen."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvFullscreenDrawArea, tr("Reject Full Draw Area"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw area is fullscreen."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvFullscreenScissor, tr("Require Full Scissor"), tr("Unchecked"),
			tr("Disable stereoscopy unless the scissor covers the fullscreen rectangle."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvFullscreenScissor, tr("Reject Full Scissor"), tr("Unchecked"),
			tr("Disable stereoscopy when the scissor covers the fullscreen rectangle."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvNoAlphaBlend, tr("Require No Alpha Blend"), tr("Unchecked"),
			tr("Disable stereoscopy unless alpha blending is disabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvNoAlphaBlend, tr("Reject No Alpha Blend"), tr("Unchecked"),
			tr("Disable stereoscopy when alpha blending is disabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvNoAlphaTest, tr("Require No Alpha Test"), tr("Unchecked"),
			tr("Disable stereoscopy unless alpha testing is disabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvNoAlphaTest, tr("Reject No Alpha Test"), tr("Unchecked"),
			tr("Disable stereoscopy when alpha testing is disabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvNoDepthTest, tr("Require No Z Test"), tr("Unchecked"),
			tr("Disable stereoscopy unless Z testing is disabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvNoDepthTest, tr("Reject No Z Test"), tr("Unchecked"),
			tr("Disable stereoscopy when Z testing is disabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvNoDepthWrite, tr("Require No Z Write"), tr("Unchecked"),
			tr("Disable stereoscopy unless Z writes are disabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvNoDepthWrite, tr("Reject No Z Write"), tr("Unchecked"),
			tr("Disable stereoscopy when Z writes are disabled."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvNoDepthOutput, tr("Require No Depth Output"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw does not write depth."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvNoDepthOutput, tr("Reject No Depth Output"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw does not write depth."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvNoDepthRead, tr("Require No Depth Read"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw does not read depth."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvNoDepthRead, tr("Reject No Depth Read"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw does not read depth."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvNoFbMask, tr("Require No FB Mask"), tr("Unchecked"),
			tr("Disable stereoscopy unless the framebuffer mask is clear."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvNoFbMask, tr("Reject No FB Mask"), tr("Unchecked"),
			tr("Disable stereoscopy when the framebuffer mask is clear."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvColorOutput, tr("Require Color Output"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw outputs color."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvColorOutput, tr("Reject Color Output"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw outputs color."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvSourceNotFromTarget, tr("Require Source Not From Target"), tr("Unchecked"),
			tr("Disable stereoscopy unless the texture source is not a render target."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvSourceNotFromTarget, tr("Reject Source Not From Target"), tr("Unchecked"),
			tr("Disable stereoscopy when the texture source is not a render target."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvDrawMatchesTex, tr("Require Draw Matches Texture"), tr("Unchecked"),
			tr("Disable stereoscopy unless draw and texture sizes match (within 2px)."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvDrawMatchesTex, tr("Reject Draw Matches Texture"), tr("Unchecked"),
			tr("Disable stereoscopy when draw and texture sizes match (within 2px)."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvNoShuffle, tr("Require No Shuffle"), tr("Unchecked"),
			tr("Disable stereoscopy unless no shuffle path is active."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvNoShuffle, tr("Reject No Shuffle"), tr("Unchecked"),
			tr("Disable stereoscopy when no shuffle path is active."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvNoMipmap, tr("Require No Mipmap"), tr("Unchecked"),
			tr("Disable stereoscopy unless mipmapping is inactive."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvNoMipmap, tr("Reject No Mipmap"), tr("Unchecked"),
			tr("Disable stereoscopy when mipmapping is inactive."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvLinearSampling, tr("Require Linear Sampling"), tr("Unchecked"),
			tr("Disable stereoscopy unless linear sampling is used."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvLinearSampling, tr("Reject Linear Sampling"), tr("Unchecked"),
			tr("Disable stereoscopy when linear sampling is used."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvEeUpload, tr("Require EE Upload"), tr("Unchecked"),
			tr("Disable stereoscopy unless the texture was recently uploaded by EE transfers."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvEeUpload, tr("Reject EE Upload"), tr("Unchecked"),
			tr("Disable stereoscopy when the texture was recently uploaded by EE transfers."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvDisplayMatch, tr("Require Display Match"), tr("Unchecked"),
			tr("Disable stereoscopy unless the draw matches an active display buffer."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvDisplayMatch, tr("Reject Display Match"), tr("Unchecked"),
			tr("Disable stereoscopy when the draw matches an active display buffer."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvRecentEeUpload, tr("Require Recent EE Upload"), tr("Unchecked"),
			tr("Disable stereoscopy unless a matching EE upload occurred within the last 5 draws."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvRecentEeUpload, tr("Reject Recent EE Upload"), tr("Unchecked"),
			tr("Disable stereoscopy when a matching EE upload occurred within the last 5 draws."));
		dialog()->registerWidgetHelp(m_hw.stereoRequireFmvRecentTransferDraw, tr("Require Recent Transfer Draw"), tr("Unchecked"),
			tr("Disable stereoscopy unless a transfer happened within the last 2 draws."));
		dialog()->registerWidgetHelp(m_hw.stereoRejectFmvRecentTransferDraw, tr("Reject Recent Transfer Draw"), tr("Unchecked"),
			tr("Disable stereoscopy when a transfer happened within the last 2 draws."));

		dialog()->registerWidgetHelp(m_advanced.texturePreloading, tr("Texture Preloading"), tr("Full (Hash Cache)"),
			tr("Uploads entire textures at once instead of in small pieces, avoiding redundant uploads when possible. "
			   "Improves performance in most games, but can make a small selection slower."));

		dialog()->registerWidgetHelp(m_fixes.gpuPaletteConversion, tr("GPU Palette Conversion"), tr("Unchecked"),
			tr("When enabled the GPU will convert colormap textures, otherwise the CPU will. "
			   "It is a trade-off between GPU and CPU."));

		dialog()->registerWidgetHelp(m_hw.enableHWFixes, tr("Manual Hardware Renderer Fixes"), tr("Unchecked"),
			tr("Enabling this option gives you the ability to change the renderer and upscaling fixes "
			   "to your games. However IF you have ENABLED this, you WILL DISABLE AUTOMATIC "
			   "SETTINGS and you can re-enable automatic settings by unchecking this option."));

		dialog()->registerWidgetHelp(m_advanced.spinCPUDuringReadbacks, tr("Spin CPU During Readbacks"), tr("Unchecked"),
			tr("Does useless work on the CPU during readbacks to prevent it from going to into powersave modes. "
			   "May improve performance during readbacks but with a significant increase in power usage."));

		dialog()->registerWidgetHelp(m_advanced.spinGPUDuringReadbacks, tr("Spin GPU During Readbacks"), tr("Unchecked"),
			tr("Submits useless work to the GPU during readbacks to prevent it from going into powersave modes. "
			   "May improve performance during readbacks but with a significant increase in power usage."));

		// Software
		dialog()->registerWidgetHelp(m_sw.extraSWThreads, tr("Software Rendering Threads"), tr("2 threads"),
			tr("Number of rendering threads: 0 for single thread, 2 or more for multithread (1 is for debugging). "
			   "2 to 4 threads is recommended, any more than that is likely to be slower instead of faster."));

		dialog()->registerWidgetHelp(m_sw.swAutoFlush, tr("Auto Flush"), tr("Checked"),
			tr("Forces a primitive flush when a framebuffer is also an input texture. "
			   "Fixes some processing effects such as the shadows in the Jak series and radiosity in GTA:SA."));

		dialog()->registerWidgetHelp(
			m_sw.swMipmap, tr("Mipmapping"), tr("Checked"), tr("Enables mipmapping, which some games require to render correctly."));
	}

	// Hardware Fixes tab
	{
		dialog()->registerWidgetHelp(m_fixes.cpuSpriteRenderBW, tr("CPU Sprite Render Size"), tr("0 (Disabled)"),
			tr("The maximum target memory width that will allow the CPU Sprite Renderer to activate on."));

		dialog()->registerWidgetHelp(m_fixes.cpuCLUTRender, tr("Software CLUT Render"), tr("0 (Disabled)"),
			tr("Tries to detect when a game is drawing its own color palette and then renders it in software, instead of on the GPU."));

		dialog()->registerWidgetHelp(m_fixes.gpuTargetCLUTMode, tr("GPU Target CLUT"), tr("Disabled"),
			tr("Tries to detect when a game is drawing its own color palette and then renders it on the GPU with special handling."));

		dialog()->registerWidgetHelp(m_fixes.skipDrawStart, tr("Skip Draw Range Start"), tr("0"),
			tr("Completely skips drawing surfaces from the surface in the left box up to the surface specified in the box on the right."));

		dialog()->registerWidgetHelp(m_fixes.skipDrawEnd, tr("Skip Draw Range End"), tr("0"),
			tr("Completely skips drawing surfaces from the surface in the left box up to the surface specified in the box on the right."));

		dialog()->registerWidgetHelp(m_fixes.hwAutoFlush, tr("Auto Flush"), tr("Unchecked"),
			tr("Forces a primitive flush when a framebuffer is also an input texture. "
			   "Fixes some processing effects such as the shadows in the Jak series and radiosity in GTA:SA."));

		dialog()->registerWidgetHelp(m_fixes.disableDepthEmulation, tr("Disable Depth Conversion"), tr("Unchecked"),
			tr("Disables the support of depth buffers in the texture cache. "
			   "Will likely create various glitches and is only useful for debugging."));

		dialog()->registerWidgetHelp(m_fixes.disableSafeFeatures, tr("Disable Safe Features"), tr("Unchecked"),
			tr("This option disables multiple safe features. "
			   "Disables accurate Unscale Point and Line rendering which can help Xenosaga games. "
			   "Disables accurate GS Memory Clearing to be done on the CPU, and lets the GPU handle it, which can help Kingdom Hearts "
			   "games."));

		dialog()->registerWidgetHelp(
			m_fixes.disableRenderFixes, tr("Disable Render Fixes"), tr("Unchecked"), tr("This option disables game-specific render fixes."));

		dialog()->registerWidgetHelp(m_fixes.disablePartialInvalidation, tr("Disable Partial Source Invalidation"), tr("Unchecked"),
			tr("By default, the texture cache handles partial invalidations. Unfortunately it is very costly to compute CPU wise. "
			   "This hack replaces the partial invalidation with a complete deletion of the texture to reduce the CPU load. "
			   "It helps with the Snowblind engine games."));
		dialog()->registerWidgetHelp(m_fixes.frameBufferConversion, tr("Framebuffer Conversion"), tr("Unchecked"),
			tr("Convert 4-bit and 8-bit framebuffer on the CPU instead of the GPU. "
			   "Helps Harry Potter and Stuntman games. It has a big impact on performance."));

		dialog()->registerWidgetHelp(m_fixes.preloadFrameData, tr("Preload Frame Data"), tr("Unchecked"),
			tr("Uploads GS data when rendering a new frame to reproduce some effects accurately."));

		dialog()->registerWidgetHelp(m_fixes.textureInsideRt, tr("Texture Inside RT"), tr("Disabled"),
			tr("Allows the texture cache to reuse as an input texture the inner portion of a previous framebuffer."));

		dialog()->registerWidgetHelp(m_fixes.limit24BitDepth, tr("Limit Depth to 24 Bits"), tr("Disabled"),
			tr("Truncate 32-bit depth values to 24 bits. Helps games struggling with Z-fighting."));

		dialog()->registerWidgetHelp(m_fixes.readTCOnClose, tr("Read Targets When Closing"), tr("Unchecked"),
			tr("Flushes all targets in the texture cache back to local memory when shutting down. Can prevent lost visuals when saving "
			   "state or switching graphics APIs, but can also cause graphical corruption."));

		dialog()->registerWidgetHelp(m_fixes.estimateTextureRegion, tr("Estimate Texture Region"), tr("Unchecked"),
			tr("Attempts to reduce the texture size when games do not set it themselves (e.g. Snowblind games)."));
	}

	// Upscaling Fixes tab
	{
		dialog()->registerWidgetHelp(m_upscaling.halfPixelOffset, tr("Half Pixel Offset"), tr("Off (Default)"),
			tr("Might fix some misaligned fog, bloom, or blend effect."));

		dialog()->registerWidgetHelp(m_upscaling.roundSprite, tr("Round Sprite"), tr("Off (Default)"),
			tr("Corrects the sampling of 2D sprite textures when upscaling. "
			   "Fixes lines in sprites of games like Ar tonelico when upscaling. Half option is for flat sprites, Full is for all "
			   "sprites."));

		dialog()->registerWidgetHelp(m_upscaling.textureOffsetX, tr("Texture Offsets X"), tr("0"),
			//: ST and UV are different types of texture coordinates, like XY would be spatial coordinates.
			tr("Offset for the ST/UV texture coordinates. Fixes some odd texture issues and might fix some post processing alignment "
			   "too."));

		dialog()->registerWidgetHelp(m_upscaling.textureOffsetY, tr("Texture Offsets Y"), tr("0"),
			//: ST and UV are different types of texture coordinates, like XY would be spatial coordinates.
			tr("Offset for the ST/UV texture coordinates. Fixes some odd texture issues and might fix some post processing alignment "
			   "too."));

		dialog()->registerWidgetHelp(m_upscaling.alignSprite, tr("Align Sprite"), tr("Unchecked"),
			//: Namco: a game publisher and development company. Leave the name as-is. Ace Combat, Tekken, Soul Calibur: game names. Leave as-is or use official translations.
			tr("Fixes issues with upscaling (vertical lines) in Namco games like Ace Combat, Tekken, Soul Calibur, etc."));

		dialog()->registerWidgetHelp(m_upscaling.forceEvenSpritePosition, tr("Force Even Sprite Position"), tr("Unchecked"),
			//: Wild Arms: name of a game series. Leave as-is or use an official translation.
			tr("Lowers the GS precision to avoid gaps between pixels when upscaling. Fixes the text on Wild Arms games."));

		dialog()->registerWidgetHelp(m_upscaling.bilinearHack, tr("Bilinear Dirty Upscale"), tr("Unchecked"),
			tr("Can smooth out textures due to be bilinear filtered when upscaling. E.g. Brave sun glare."));

		dialog()->registerWidgetHelp(m_upscaling.mergeSprite, tr("Merge Sprite"), tr("Unchecked"),
			tr("Replaces post-processing multiple paving sprites by a single fat sprite. It reduces various upscaling lines."));

		dialog()->registerWidgetHelp(m_upscaling.nativePaletteDraw, tr("Unscaled Palette Texture Draws"), tr("Unchecked"),
			tr("Forces palette texture draws to render at native resolution."));
	}

	// Texture Replacement tab
	{
		dialog()->registerWidgetHelp(m_texture.dumpReplaceableTextures, tr("Dump Textures"), tr("Unchecked"), tr("Dumps replaceable textures to disk. Will reduce performance."));

		dialog()->registerWidgetHelp(m_texture.dumpReplaceableMipmaps, tr("Dump Mipmaps"), tr("Unchecked"), tr("Includes mipmaps when dumping textures."));

		dialog()->registerWidgetHelp(m_texture.dumpTexturesWithFMVActive, tr("Dump FMV Textures"), tr("Unchecked"), tr("Allows texture dumping when FMVs are active. You should not enable this."));

		dialog()->registerWidgetHelp(m_texture.loadTextureReplacementsAsync, tr("Asynchronous Texture Loading"), tr("Checked"), tr("Loads replacement textures on a worker thread, reducing microstutter when replacements are enabled."));

		dialog()->registerWidgetHelp(m_texture.loadTextureReplacements, tr("Load Textures"), tr("Unchecked"), tr("Loads replacement textures where available and user-provided."));

		dialog()->registerWidgetHelp(m_texture.precacheTextureReplacements, tr("Precache Textures"), tr("Unchecked"), tr("Preloads all replacement textures to memory. Not necessary with asynchronous loading."));
	}

	// Post Processing tab
	{
		//: You might find an official translation for this on AMD's website (Spanish version linked): https://www.amd.com/es/technologies/radeon-software-fidelityfx
		dialog()->registerWidgetHelp(m_post.casMode, tr("Contrast Adaptive Sharpening"), tr("None (Default)"), tr("Enables FidelityFX Contrast Adaptive Sharpening."));

		dialog()->registerWidgetHelp(m_post.casSharpness, tr("Sharpness"), tr("50%"), tr("Determines the intensity the sharpening effect in CAS post-processing."));

		dialog()->registerWidgetHelp(m_post.shadeBoost, tr("Shade Boost"), tr("Unchecked"),
			tr("Enables saturation, contrast, and brightness to be adjusted. Values of brightness, saturation, and contrast are at default "
			   "50."));

		dialog()->registerWidgetHelp(
			m_post.fxaa, tr("FXAA"), tr("Unchecked"), tr("Applies the FXAA anti-aliasing algorithm to improve the visual quality of games."));

		dialog()->registerWidgetHelp(m_post.shadeBoostBrightness, tr("Brightness"), tr("50"), tr("Adjusts brightness. 50 is normal."));

		dialog()->registerWidgetHelp(m_post.shadeBoostContrast, tr("Contrast"), tr("50"), tr("Adjusts contrast. 50 is normal."));

		dialog()->registerWidgetHelp(m_post.shadeBoostGamma, tr("Gamma"), tr("50"), tr("Adjusts gamma. 50 is normal."));

		dialog()->registerWidgetHelp(m_post.shadeBoostSaturation, tr("Saturation"), tr("50"), tr("Adjusts saturation. 50 is normal."));

		dialog()->registerWidgetHelp(m_post.tvShader, tr("TV Shader"), tr("None (Default)"),
			tr("Applies a shader which replicates the visual effects of different styles of television sets."));
	}

	// OSD tab
	{
		dialog()->registerWidgetHelp(m_osd.scale, tr("OSD Scale"), tr("100%"), tr("Scales the size of the onscreen OSD from 50% to 500%."));

		dialog()->registerWidgetHelp(m_osd.messagesPos, tr("OSD Messages Position"), tr("Left (Default)"),
			tr("Position of on-screen-display messages when events occur such as save states being "
			   "created/loaded, screenshots being taken, etc."));

		dialog()->registerWidgetHelp(m_osd.performancePos, tr("OSD Performance Position"), tr("Right (Default)"),
			tr("Position of a variety of on-screen performance data points as selected by the user."));

		dialog()->registerWidgetHelp(m_osd.showSpeedPercentages, tr("Show Speed Percentages"), tr("Unchecked"),
			tr("Shows the current emulation speed of the system as a percentage."));

		dialog()->registerWidgetHelp(m_osd.showFPS, tr("Show FPS"), tr("Unchecked"),
			tr("Shows the number of internal video frames displayed per second by the system."));

		dialog()->registerWidgetHelp(m_osd.showVPS, tr("Show VPS"), tr("Unchecked"),
			tr("Shows the number of Vsyncs performed per second by the system."));

		dialog()->registerWidgetHelp(m_osd.showResolution, tr("Show Resolution"), tr("Unchecked"),
			tr("Shows the internal resolution of the game."));

		dialog()->registerWidgetHelp(m_osd.showGSStats, tr("Show GS Statistics"), tr("Unchecked"),
			tr("Shows statistics about the emulated GS such as primitives and draw calls."));

		dialog()->registerWidgetHelp(m_osd.showUsageCPU, tr("Show CPU Usage"),
			tr("Unchecked"), tr("Shows the host's CPU utilization based on threads."));

		dialog()->registerWidgetHelp(m_osd.showUsageGPU, tr("Show GPU Usage"),
			tr("Unchecked"), tr("Shows the host's GPU utilization."));

		dialog()->registerWidgetHelp(m_osd.showStatusIndicators, tr("Show Status Indicators"), tr("Checked"),
			tr("Shows icon indicators for emulation states such as Pausing, Turbo, Fast-Forward, and Slow-Motion."));

		dialog()->registerWidgetHelp(m_osd.showFrameTimes, tr("Show Frame Times"), tr("Unchecked"),
			tr("Displays a graph showing the average frametimes."));

		dialog()->registerWidgetHelp(m_osd.showHardwareInfo, tr("Show Hardware Info"), tr("Unchecked"),
			tr("Shows the current system CPU and GPU information."));

		dialog()->registerWidgetHelp(m_osd.showVersion, tr("Show PCSX2 Version"), tr("Unchecked"),
			tr("Shows the current PCSX2 version."));

		dialog()->registerWidgetHelp(m_osd.showSettings, tr("Show Settings"), tr("Unchecked"),
			tr("Displays various settings and the current values of those settings in the bottom-right corner of the display."));

		dialog()->registerWidgetHelp(m_osd.showPatches, tr("Show Patches"), tr("Unchecked"),
			tr("Shows the amount of currently active patches/cheats in the bottom-right corner of the display."));

		dialog()->registerWidgetHelp(m_osd.showInputs, tr("Show Inputs"), tr("Unchecked"),
			tr("Shows the current controller state of the system in the bottom-left corner of the display."));

		dialog()->registerWidgetHelp(m_osd.showVideoCapture, tr("Show Video Capture Status"), tr("Checked"),
			tr("Shows the status of the currently active video capture in the top-right corner of the display."));

		dialog()->registerWidgetHelp(m_osd.showInputRec, tr("Show Input Recording Status"), tr("Checked"),
			tr("Shows the status of the currently active input recording in the top-right corner of the display."));

		dialog()->registerWidgetHelp(m_osd.showTextureReplacements, tr("Show Texture Replacement Status"), tr("Unchecked"),
			tr("Shows the status of the number of dumped and loaded texture replacements in the top-right corner of the display."));

		dialog()->registerWidgetHelp(m_osd.warnAboutUnsafeSettings, tr("Warn About Unsafe Settings"), tr("Checked"),
			tr("Displays warnings when settings are enabled which may break games."));

		connect(m_osd.showSettings, &QCheckBox::checkStateChanged, this,
			&GraphicsSettingsWidget::onOsdShowSettingsToggled);
	}

	// Recording tab
	{
		dialog()->registerWidgetHelp(m_capture.videoCaptureCodec, tr("Video Codec"), tr("Default"),
			tr("Selects the Video Codec to be used for Video Capture. "
			   "<b>If unsure, leave it on default.<b>"));

		dialog()->registerWidgetHelp(m_capture.videoCaptureFormat, tr("Video Format"), tr("Default"),
			tr("Selects the Video Format to be used for Video Capture. If by chance the codec does not support the format, the first format available will be used. "
			   "<b>If unsure, leave it on default.<b>"));

		dialog()->registerWidgetHelp(m_capture.videoCaptureBitrate, tr("Video Bitrate"), tr("6000 kbps"),
			tr("Sets the video bitrate to be used. "
			   "Higher bitrates generally yield better video quality at the cost of larger resulting file sizes."));

		dialog()->registerWidgetHelp(m_capture.videoCaptureResolutionAuto, tr("Automatic Resolution"), tr("Unchecked"),
			tr("When checked, the video capture resolution will follow the internal resolution of the running game.<br><br>"

			   "<b>Be careful when using this setting especially when you are upscaling, as higher internal resolutions (above 4x) can result in very large video capture and can cause system overload.</b>"));


		dialog()->registerWidgetHelp(m_capture.enableVideoCaptureArguments, tr("Enable Extra Video Arguments"), tr("Unchecked"), tr("Allows you to pass arguments to the selected video codec."));

		dialog()->registerWidgetHelp(m_capture.videoCaptureArguments, tr("Extra Video Arguments"), tr("Leave It Blank"),
			tr("Parameters passed to the selected video codec.<br>"
			   "<b>You must use '=' to separate key from value and ':' to separate two pairs from each other.</b><br>"
			   "For example: \"crf = 21 : preset = veryfast\""));

		dialog()->registerWidgetHelp(m_capture.audioCaptureCodec, tr("Audio Codec"), tr("Default"),
			tr("Selects the Audio Codec to be used for Video Capture. "
			   "<b>If unsure, leave it on default.<b>"));

		dialog()->registerWidgetHelp(m_capture.audioCaptureBitrate, tr("Audio Bitrate"), tr("192 kbps"), tr("Sets the audio bitrate to be used."));

		dialog()->registerWidgetHelp(m_capture.enableAudioCaptureArguments, tr("Enable Extra Audio Arguments"), tr("Unchecked"), tr("Allows you to pass arguments to the selected audio codec."));

		dialog()->registerWidgetHelp(m_capture.audioCaptureArguments, tr("Extra Audio Arguments"), tr("Leave It Blank"),
			tr("Parameters passed to the selected audio codec.<br>"
			   "<b>You must use '=' to separate key from value and ':' to separate two pairs from each other.</b><br>"
			   "For example: \"compression_level = 4 : joint_stereo = 1\""));
	}

	// Advanced tab
	{
		dialog()->registerWidgetHelp(m_advanced.gsDumpCompression, tr("GS Dump Compression"), tr("Zstandard (zst)"),
			tr("Change the compression algorithm used when creating a GS dump."));

		//: Blit = a data operation. You might want to write it as-is, but fully uppercased. More information: https://en.wikipedia.org/wiki/Bit_blit \nSwap chain: see Microsoft's Terminology Portal.
		dialog()->registerWidgetHelp(m_advanced.useBlitSwapChain, tr("Use Blit Swap Chain"), tr("Unchecked"),
			//: Blit = a data operation. You might want to write it as-is, but fully uppercased. More information: https://en.wikipedia.org/wiki/Bit_blit
			tr("Uses a blit presentation model instead of flipping when using the Direct3D 11 "
			   "graphics API. This usually results in slower performance, but may be required for some "
			   "streaming applications, or to uncap framerates on some systems."));

		dialog()->registerWidgetHelp(m_advanced.exclusiveFullscreenControl, tr("Allow Exclusive Fullscreen"), tr("Automatic (Default)"),
			tr("Overrides the driver's heuristics for enabling exclusive fullscreen, or direct flip/scanout.<br>"
			   "Disallowing exclusive fullscreen may enable smoother task switching and overlays, but increase input latency."));

		dialog()->registerWidgetHelp(m_advanced.disableMailboxPresentation, tr("Disable Mailbox Presentation"), tr("Unchecked"),
			tr("Forces the use of FIFO over Mailbox presentation, i.e. double buffering instead of triple buffering. "
			   "Usually results in worse frame pacing."));

		dialog()->registerWidgetHelp(m_advanced.extendedUpscales, tr("Extended Upscaling Multipliers"), tr("Unchecked"),
			tr("Displays additional, very high upscaling multipliers dependent on GPU capability."));

		dialog()->registerWidgetHelp(m_advanced.useDebugDevice, tr("Enable Debug Device"), tr("Unchecked"),
			tr("Enables API-level validation of graphics commands."));

		dialog()->registerWidgetHelp(m_advanced.gsDownloadMode, tr("GS Download Mode"), tr("Accurate"),
			tr("Skips synchronizing with the GS thread and host GPU for GS downloads. "
			   "Can result in a large speed boost on slower systems, at the cost of many broken graphical effects. "
			   "If games are broken and you have this option enabled, please disable it first."));

		dialog()->registerWidgetHelp(m_advanced.ntscFrameRate, tr("NTSC Frame Rate"), tr("59.94 Hz"),
			tr("Determines what frame rate NTSC games run at."));

		dialog()->registerWidgetHelp(m_advanced.palFrameRate, tr("PAL Frame Rate"), tr("50.00 Hz"),
			tr("Determines what frame rate PAL games run at."));
	}
}

GraphicsSettingsWidget::~GraphicsSettingsWidget() = default;

void GraphicsSettingsWidget::onTextureFilteringChange()
{
	const QSignalBlocker block(m_sw.swTextureFiltering);

	m_sw.swTextureFiltering->setCurrentIndex(m_hw.textureFiltering->currentIndex());
}

void GraphicsSettingsWidget::onSWTextureFilteringChange()
{
	const QSignalBlocker block(m_hw.textureFiltering);

	m_hw.textureFiltering->setCurrentIndex(m_sw.swTextureFiltering->currentIndex());
}

void GraphicsSettingsWidget::onRendererChanged(int index)
{
	if (dialog()->isPerGameSettings())
	{
		if (index > 0)
			dialog()->setIntSettingValue("EmuCore/GS", "Renderer", static_cast<int>(s_renderer_info[index - 1].type));
		else
			dialog()->setIntSettingValue("EmuCore/GS", "Renderer", std::nullopt);
	}
	else
	{
		dialog()->setIntSettingValue("EmuCore/GS", "Renderer", static_cast<int>(s_renderer_info[index].type));
	}

	g_emu_thread->applySettings();
	updateRendererDependentOptions();
}

void GraphicsSettingsWidget::onAdapterChanged(int index)
{
	const int first_adapter = dialog()->isPerGameSettings() ? 2 : 1;

	if (index >= first_adapter)
		dialog()->setStringSettingValue("EmuCore/GS", "Adapter", m_header.adapterDropdown->currentText().toUtf8().constData());
	else if (index > 0 && dialog()->isPerGameSettings())
		dialog()->setStringSettingValue("EmuCore/GS", "Adapter", "");
	else
		dialog()->setStringSettingValue("EmuCore/GS", "Adapter", std::nullopt);

	g_emu_thread->applySettings();
}

void GraphicsSettingsWidget::onFullscreenModeChanged(int index)
{
	const int first_mode = dialog()->isPerGameSettings() ? 2 : 1;

	if (index >= first_mode)
		dialog()->setStringSettingValue("EmuCore/GS", "FullscreenMode", m_display.fullscreenModes->currentText().toUtf8().constData());
	else if (index > 0 && dialog()->isPerGameSettings())
		dialog()->setStringSettingValue("EmuCore/GS", "FullscreenMode", "");
	else
		dialog()->setStringSettingValue("EmuCore/GS", "FullscreenMode", std::nullopt);

	g_emu_thread->applySettings();
}

void GraphicsSettingsWidget::onTrilinearFilteringChanged()
{
	const bool forced_bilinear = (dialog()->getEffectiveIntValue("EmuCore/GS", "TriFilter", static_cast<int>(TriFiltering::Automatic)) >=
								  static_cast<int>(TriFiltering::Forced));
	m_hw.textureFiltering->setDisabled(forced_bilinear);
}

void GraphicsSettingsWidget::onShadeBoostChanged()
{
	const bool enabled = dialog()->getEffectiveBoolValue("EmuCore/GS", "ShadeBoost", false);
	m_post.shadeBoostBrightness->setEnabled(enabled);
	m_post.shadeBoostContrast->setEnabled(enabled);
	m_post.shadeBoostGamma->setEnabled(enabled);
	m_post.shadeBoostSaturation->setEnabled(enabled);
}

void GraphicsSettingsWidget::onStereoscopicModeChanged()
{
	const std::string effective_mode = dialog()->getEffectiveStringValue("EmuCore/GS", "StereoMode", "Off");
	const bool stereo_enabled = (effective_mode != "Off");
	m_hw.stereoDominantEyeLabel->setEnabled(stereo_enabled);
	m_hw.stereoDominantEye->setEnabled(stereo_enabled);
	m_hw.stereoSeparationLabel->setEnabled(stereo_enabled);
	m_hw.stereoSeparation->setEnabled(stereo_enabled);
	m_hw.stereoConvergenceLabel->setEnabled(stereo_enabled);
	m_hw.stereoConvergence->setEnabled(stereo_enabled);
	m_hw.stereoDepthFactorLabel->setEnabled(stereo_enabled);
	m_hw.stereoDepthFactor->setEnabled(stereo_enabled);
	m_hw.stereoUiDepth->setEnabled(stereo_enabled);
	m_hw.stereoUiDepthLabel->setEnabled(stereo_enabled);
	m_hw.stereoUiDepthValue->setEnabled(stereo_enabled);
	m_hw.stereoUiSecondLayerDepth->setEnabled(stereo_enabled);
	m_hw.stereoUiSecondLayerDepthLabel->setEnabled(stereo_enabled);
	m_hw.stereoUiSecondLayerDepthValue->setEnabled(stereo_enabled);
	m_hw.stereoSwapEyes->setEnabled(stereo_enabled);
	m_hw.stereoFlipRendering->setEnabled(stereo_enabled);
	m_hw.stereoDontRenderMonoObjects->setEnabled(stereo_enabled);
	m_hw.stereoRejectNonPositiveZ->setEnabled(stereo_enabled);
	m_hw.stereoRejectSmallZRange->setEnabled(stereo_enabled);
	m_hw.stereoRejectSpriteBlit->setEnabled(stereo_enabled);
	m_hw.stereoRejectConstantColor->setEnabled(stereo_enabled);
	m_hw.stereoRejectScalingDraw->setEnabled(stereo_enabled);
	m_hw.stereoRejectSbsInput->setEnabled(stereo_enabled);
	m_hw.stereoRejectTabInput->setEnabled(stereo_enabled);
	m_hw.stereoRequireDisplayBuffer1->setEnabled(stereo_enabled);
	m_hw.stereoRequireDisplayBuffer2->setEnabled(stereo_enabled);
    m_hw.stereoFixStencilShadows->setEnabled(stereo_enabled);
	m_hw.stereoRequirePerspectiveUV->setEnabled(stereo_enabled);
	m_hw.stereoRequireZVaries->setEnabled(stereo_enabled);
	m_hw.stereoRequireDepthActive->setEnabled(stereo_enabled);
	m_hw.stereoRejectSprites->setEnabled(stereo_enabled);
	m_hw.stereoRejectUiLike->setEnabled(stereo_enabled);
	m_hw.stereoUiSafeDetect->setEnabled(stereo_enabled);
	m_hw.stereoUiAdvancedDetect->setEnabled(stereo_enabled);
	m_hw.stereoUiBackgroundDepth->setEnabled(stereo_enabled);
	m_hw.stereoMasterFix->setEnabled(stereo_enabled);
	m_hw.stereoMasterFixTest->setEnabled(stereo_enabled);
	m_hw.stereoMasterFix1->setEnabled(stereo_enabled);
	m_hw.stereoMasterFix2->setEnabled(stereo_enabled);
	m_hw.stereoMasterFix3->setEnabled(stereo_enabled);
	m_hw.stereoMasterFix4->setEnabled(stereo_enabled);
	m_hw.stereoMasterFix5->setEnabled(stereo_enabled);
	m_hw.stereoMasterFix6->setEnabled(stereo_enabled);
	m_hw.stereoMasterFix7->setEnabled(stereo_enabled);
	m_hw.stereoMasterFix8->setEnabled(stereo_enabled);
	m_hw.stereoMasterFix9->setEnabled(stereo_enabled);
	m_hw.stereoMasterFix10->setEnabled(stereo_enabled);
	m_hw.stereoRequireTextureMapping->setEnabled(stereo_enabled);
	m_hw.stereoRequireAlphaBlend->setEnabled(stereo_enabled);
	m_hw.stereoRequireAlphaTest->setEnabled(stereo_enabled);
	m_hw.stereoRequireUvVaries->setEnabled(stereo_enabled);
	m_hw.stereoRequireColorVaries->setEnabled(stereo_enabled);
	m_hw.stereoRequireFog->setEnabled(stereo_enabled);
	m_hw.stereoStencilRequireDate->setEnabled(stereo_enabled);
	m_hw.stereoStencilRequireDatm->setEnabled(stereo_enabled);
	m_hw.stereoStencilRequireAte->setEnabled(stereo_enabled);
	m_hw.stereoStencilRequireAfailZbOnly->setEnabled(stereo_enabled);
	m_hw.stereoStencilRequireAfailNotKeep->setEnabled(stereo_enabled);
	m_hw.stereoStencilRequireZWrite->setEnabled(stereo_enabled);
	m_hw.stereoStencilRequireZTest->setEnabled(stereo_enabled);
	m_hw.stereoStencilRequireZTestGequal->setEnabled(stereo_enabled);
	m_hw.stereoStencilRequireFbMask->setEnabled(stereo_enabled);
	m_hw.stereoStencilRequireFbMaskFull->setEnabled(stereo_enabled);
	m_hw.stereoStencilRequireTexIsFb->setEnabled(stereo_enabled);
	m_hw.stereoRejectFullscreenDraw->setEnabled(stereo_enabled);
	m_hw.stereoRejectFullscreenScissor->setEnabled(stereo_enabled);
	m_hw.stereoRejectFullCover->setEnabled(stereo_enabled);
	m_hw.stereoRejectSpriteNoGaps->setEnabled(stereo_enabled);
	m_hw.stereoRejectTexIsFb->setEnabled(stereo_enabled);
	m_hw.stereoRejectChannelShuffle->setEnabled(stereo_enabled);
	m_hw.stereoRejectTextureShuffle->setEnabled(stereo_enabled);
	m_hw.stereoRejectFullscreenShuffle->setEnabled(stereo_enabled);
	m_hw.stereoRejectShaderShuffle->setEnabled(stereo_enabled);
	m_hw.stereoRejectShuffleAcross->setEnabled(stereo_enabled);
	m_hw.stereoRejectShuffleSame->setEnabled(stereo_enabled);
	m_hw.stereoRejectChannelFetch->setEnabled(stereo_enabled);
	m_hw.stereoRejectChannelFetchFb->setEnabled(stereo_enabled);
	m_hw.stereoRejectFeedbackLoop->setEnabled(stereo_enabled);
	m_hw.stereoRejectColclip->setEnabled(stereo_enabled);
	m_hw.stereoRejectRtaCorrection->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectRtaSourceCorrection->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectColclipHw->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectColclip->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectBlendMix->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectPabe->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectFbMask->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectTexIsFb->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectNoColor->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectNoColor1->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectAemFmt->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectPalFmt->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectDstFmt->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectDepthFmt->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectAem->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectFba->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectFog->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectIip->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectDate->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectAtst->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectAfail->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectFst->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectTfx->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectTcc->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectWms->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectWmt->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectAdjs->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectAdjt->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectLtf->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectShuffle->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectShuffleSame->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectReal16Src->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectProcessBa->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectProcessRg->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectShuffleAcross->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectWriteRg->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectBlendA->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectBlendB->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectBlendC->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectBlendD->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectFixedOneA->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectBlendHw->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectAMasked->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectRoundInv->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectChannel->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectChannelFb->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectDither->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectDitherAdjust->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectZClamp->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectZFloor->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectTCOffsetHack->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectUrbanChaosHle->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectTalesOfAbyssHle->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectAutomaticLod->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectManualLod->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectPointSampler->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectRegionRect->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRejectScanmask->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireRtaCorrection->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireRtaSourceCorrection->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireColclipHw->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireColclip->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireBlendMix->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequirePabe->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireFbMask->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireTexIsFb->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireNoColor->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireNoColor1->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireAemFmt->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequirePalFmt->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireDstFmt->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireDepthFmt->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireAem->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireFba->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireFog->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireIip->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireDate->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireAtst->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireAfail->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireFst->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireTfx->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireTcc->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireWms->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireWmt->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireAdjs->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireAdjt->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireLtf->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireShuffle->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireShuffleSame->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireReal16Src->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireProcessBa->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireProcessRg->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireShuffleAcross->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireWriteRg->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireBlendA->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireBlendB->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireBlendC->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireBlendD->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireFixedOneA->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireBlendHw->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireAMasked->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireRoundInv->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireChannel->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireChannelFb->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireDither->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireDitherAdjust->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireZClamp->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireZFloor->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireTCOffsetHack->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireUrbanChaosHle->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireTalesOfAbyssHle->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireAutomaticLod->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireManualLod->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequirePointSampler->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireRegionRect->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireScanmask->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireAlphaBlend->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireAlphaTest->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireDatm->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireZTest->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireZWrite->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireZTestAlways->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireZTestNever->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireAa1->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireChannelShuffle->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireTextureShuffle->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireFullscreenShuffle->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequirePoints->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireLines->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireTriangles->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireSprites->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireFixedQ->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireFixedZ->setEnabled(stereo_enabled);
	m_hw.stereoUniversalRequireConstantColor->setEnabled(stereo_enabled);
	m_hw.stereoRejectBlendMix->setEnabled(stereo_enabled);
	m_hw.stereoRejectPabe->setEnabled(stereo_enabled);
	m_hw.stereoRejectDither->setEnabled(stereo_enabled);
	m_hw.stereoRejectScanmask->setEnabled(stereo_enabled);
	m_hw.stereoRejectRegionRect->setEnabled(stereo_enabled);
	m_hw.stereoRejectNoColorOutput->setEnabled(stereo_enabled);
	m_hw.stereoRejectHleShuffle->setEnabled(stereo_enabled);
	m_hw.stereoRejectTCOffsetHack->setEnabled(stereo_enabled);
	m_hw.stereoRejectPoints->setEnabled(stereo_enabled);
	m_hw.stereoRejectLines->setEnabled(stereo_enabled);
	m_hw.stereoRejectFlatShading->setEnabled(stereo_enabled);
	m_hw.stereoRejectFst->setEnabled(stereo_enabled);
	m_hw.stereoEnableOptions->setEnabled(stereo_enabled);
	m_hw.stereoRemoveFixedSt->setEnabled(stereo_enabled);
	m_hw.stereoRejectFixedQ->setEnabled(stereo_enabled);
	m_hw.stereoRejectAa1->setEnabled(stereo_enabled);
	m_hw.stereoRejectNoZTest->setEnabled(stereo_enabled);
	m_hw.stereoRejectNoZWrite->setEnabled(stereo_enabled);
	m_hw.stereoRejectZTestAlways->setEnabled(stereo_enabled);
	m_hw.stereoRejectZTestNever->setEnabled(stereo_enabled);
	m_hw.stereoRejectAlphaTestOff->setEnabled(stereo_enabled);
	m_hw.stereoRejectAlphaTestAlways->setEnabled(stereo_enabled);
	m_hw.stereoRejectAlphaTestNever->setEnabled(stereo_enabled);
	m_hw.stereoRejectTfxModulate->setEnabled(stereo_enabled);
	m_hw.stereoRejectTfxDecal->setEnabled(stereo_enabled);
	m_hw.stereoRejectTfxHighlight->setEnabled(stereo_enabled);
	m_hw.stereoRejectTfxHighlight2->setEnabled(stereo_enabled);
	m_hw.stereoRejectSmallDrawArea->setEnabled(stereo_enabled);
	m_hw.stereoRejectWideDrawBand->setEnabled(stereo_enabled);
	m_hw.stereoRejectTopDrawBand->setEnabled(stereo_enabled);
	m_hw.stereoRejectRtSpriteNoDepth->setEnabled(stereo_enabled);
	m_hw.stereoRejectRtSpriteAlphaBlend->setEnabled(stereo_enabled);
	m_hw.stereoRequireProcessTexture->setEnabled(stereo_enabled);
	m_hw.stereoRejectProcessTexture->setEnabled(stereo_enabled);
	m_hw.stereoRequireSourceFromTarget->setEnabled(stereo_enabled);
	m_hw.stereoRejectSourceFromTarget->setEnabled(stereo_enabled);
	m_hw.stereoRequireDrawUsesTarget->setEnabled(stereo_enabled);
	m_hw.stereoRejectDrawUsesTarget->setEnabled(stereo_enabled);
	m_hw.stereoRequireTexIsRt->setEnabled(stereo_enabled);
	m_hw.stereoRejectTexIsRt->setEnabled(stereo_enabled);
	m_hw.stereoRequireInTargetDraw->setEnabled(stereo_enabled);
	m_hw.stereoRejectInTargetDraw->setEnabled(stereo_enabled);
	m_hw.stereoRequireTempZ->setEnabled(stereo_enabled);
	m_hw.stereoRejectTempZ->setEnabled(stereo_enabled);
	m_hw.stereoRequireOneBarrier->setEnabled(stereo_enabled);
	m_hw.stereoRejectOneBarrier->setEnabled(stereo_enabled);
	m_hw.stereoRequireFullBarrier->setEnabled(stereo_enabled);
	m_hw.stereoRejectFullBarrier->setEnabled(stereo_enabled);
	m_hw.stereoRequireSinglePass->setEnabled(stereo_enabled);
	m_hw.stereoRejectSinglePass->setEnabled(stereo_enabled);
	m_hw.stereoRequireFullscreenDrawArea->setEnabled(stereo_enabled);
	m_hw.stereoRejectFullscreenDrawArea->setEnabled(stereo_enabled);
	m_hw.stereoRequireFullscreenSprite->setEnabled(stereo_enabled);
	m_hw.stereoRejectFullscreenSprite->setEnabled(stereo_enabled);
	m_hw.stereoRequireTexturedSprite->setEnabled(stereo_enabled);
	m_hw.stereoRejectTexturedSprite->setEnabled(stereo_enabled);
	m_hw.stereoRequireRtOutput->setEnabled(stereo_enabled);
	m_hw.stereoRejectRtOutput->setEnabled(stereo_enabled);
	m_hw.stereoRequireDepthOutput->setEnabled(stereo_enabled);
	m_hw.stereoRejectDepthOutput->setEnabled(stereo_enabled);
	m_hw.stereoRequireDepthRead->setEnabled(stereo_enabled);
	m_hw.stereoRejectDepthRead->setEnabled(stereo_enabled);
	m_hw.stereoRequireDepthWrite->setEnabled(stereo_enabled);
	m_hw.stereoRejectDepthWrite->setEnabled(stereo_enabled);
	m_hw.stereoRequirePalettedTexture->setEnabled(stereo_enabled);
	m_hw.stereoRejectPalettedTexture->setEnabled(stereo_enabled);
	m_hw.stereoRequireDepthTexture->setEnabled(stereo_enabled);
	m_hw.stereoRejectDepthTexture->setEnabled(stereo_enabled);
	m_hw.stereoRequireMipmap->setEnabled(stereo_enabled);
	m_hw.stereoRejectMipmap->setEnabled(stereo_enabled);
	m_hw.stereoRequireLinearSampling->setEnabled(stereo_enabled);
	m_hw.stereoRejectLinearSampling->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvActive->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvActive->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvHeuristic->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvHeuristic->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvSprite->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvSprite->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvSingleSprite->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvSingleSprite->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvTextureMapping->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvTextureMapping->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvProcessTexture->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvProcessTexture->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvFullscreenDrawArea->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvFullscreenDrawArea->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvFullscreenScissor->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvFullscreenScissor->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvNoAlphaBlend->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvNoAlphaBlend->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvNoAlphaTest->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvNoAlphaTest->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvNoDepthTest->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvNoDepthTest->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvNoDepthWrite->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvNoDepthWrite->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvNoDepthOutput->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvNoDepthOutput->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvNoDepthRead->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvNoDepthRead->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvNoFbMask->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvNoFbMask->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvColorOutput->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvColorOutput->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvSourceNotFromTarget->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvSourceNotFromTarget->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvDrawMatchesTex->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvDrawMatchesTex->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvNoShuffle->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvNoShuffle->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvNoMipmap->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvNoMipmap->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvLinearSampling->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvLinearSampling->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvEeUpload->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvEeUpload->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvDisplayMatch->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvDisplayMatch->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvRecentEeUpload->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvRecentEeUpload->setEnabled(stereo_enabled);
	m_hw.stereoRequireFmvRecentTransferDraw->setEnabled(stereo_enabled);
	m_hw.stereoRejectFmvRecentTransferDraw->setEnabled(stereo_enabled);
}

void GraphicsSettingsWidget::onMessagesPosChanged()
{
	const bool enabled = m_osd.messagesPos->currentIndex() != (dialog()->isPerGameSettings() ? 1 : 0);

	m_osd.warnAboutUnsafeSettings->setEnabled(enabled);
}

void GraphicsSettingsWidget::onPerformancePosChanged()
{
	const bool enabled = m_osd.performancePos->currentIndex() != (dialog()->isPerGameSettings() ? 1 : 0);

	m_osd.showSpeedPercentages->setEnabled(enabled);
	m_osd.showFPS->setEnabled(enabled);
	m_osd.showVPS->setEnabled(enabled);
	m_osd.showResolution->setEnabled(enabled);
	m_osd.showGSStats->setEnabled(enabled);
	m_osd.showUsageCPU->setEnabled(enabled);
	m_osd.showUsageGPU->setEnabled(enabled);
	m_osd.showStatusIndicators->setEnabled(enabled);
	m_osd.showFrameTimes->setEnabled(enabled);
	m_osd.showHardwareInfo->setEnabled(enabled);
	m_osd.showVersion->setEnabled(enabled);
}

void GraphicsSettingsWidget::onTextureDumpChanged()
{
	const bool enabled = dialog()->getEffectiveBoolValue("EmuCore/GS", "DumpReplaceableTextures", false);
	m_texture.dumpReplaceableMipmaps->setEnabled(enabled);
	m_texture.dumpTexturesWithFMVActive->setEnabled(enabled);
}

void GraphicsSettingsWidget::onTextureReplacementChanged()
{
	const bool enabled = dialog()->getEffectiveBoolValue("EmuCore/GS", "LoadTextureReplacements", false);
	m_texture.loadTextureReplacementsAsync->setEnabled(enabled);
	m_texture.precacheTextureReplacements->setEnabled(enabled);
}

void GraphicsSettingsWidget::onCaptureContainerChanged()
{
	const std::string container(
		dialog()->getEffectiveStringValue("EmuCore/GS", "CaptureContainer", Pcsx2Config::GSOptions::DEFAULT_CAPTURE_CONTAINER));

	QObject::disconnect(m_capture.videoCaptureCodec, &QComboBox::currentIndexChanged, nullptr, nullptr);
	m_capture.videoCaptureCodec->clear();
	//: This string refers to a default codec, whether it's an audio codec or a video codec.
	m_capture.videoCaptureCodec->addItem(tr("Default"), QString());
	for (const auto& [format, name] : GSCapture::GetVideoCodecList(container.c_str()))
	{
		const QString qformat(QString::fromStdString(format));
		const QString qname(QString::fromStdString(name));
		m_capture.videoCaptureCodec->addItem(QStringLiteral("%1 [%2]").arg(qformat).arg(qname), qformat);
	}

	SettingWidgetBinder::BindWidgetToStringSetting(
		dialog()->getSettingsInterface(), m_capture.videoCaptureCodec, "EmuCore/GS", "VideoCaptureCodec");
	connect(m_capture.videoCaptureCodec, &QComboBox::currentIndexChanged, this, &GraphicsSettingsWidget::onCaptureCodecChanged);

	QObject::disconnect(m_capture.audioCaptureCodec, &QComboBox::currentIndexChanged, nullptr, nullptr);
	m_capture.audioCaptureCodec->clear();
	m_capture.audioCaptureCodec->addItem(tr("Default"), QString());
	for (const auto& [format, name] : GSCapture::GetAudioCodecList(container.c_str()))
	{
		const QString qformat(QString::fromStdString(format));
		const QString qname(QString::fromStdString(name));
		m_capture.audioCaptureCodec->addItem(QStringLiteral("%1 [%2]").arg(qformat).arg(qname), qformat);
	}

	SettingWidgetBinder::BindWidgetToStringSetting(
		dialog()->getSettingsInterface(), m_capture.audioCaptureCodec, "EmuCore/GS", "AudioCaptureCodec");
}

void GraphicsSettingsWidget::GraphicsSettingsWidget::onCaptureCodecChanged()
{
	QObject::disconnect(m_capture.videoCaptureFormat, &QComboBox::currentIndexChanged, nullptr, nullptr);
	m_capture.videoCaptureFormat->clear();
	//: This string refers to a default pixel format
	m_capture.videoCaptureFormat->addItem(tr("Default"), "");

	const std::string codec(
		dialog()->getEffectiveStringValue("EmuCore/GS", "VideoCaptureCodec", ""));

	if (!codec.empty())
	{
		for (const auto& [id, name] : GSCapture::GetVideoFormatList(codec.c_str()))
		{
			const QString qid(QString::number(id));
			const QString qname(QString::fromStdString(name));
			m_capture.videoCaptureFormat->addItem(qname, qid);
		}
	}

	SettingWidgetBinder::BindWidgetToStringSetting(
		dialog()->getSettingsInterface(), m_capture.videoCaptureFormat, "EmuCore/GS", "VideoCaptureFormat");
}

void GraphicsSettingsWidget::onEnableVideoCaptureChanged()
{
	const bool enabled = dialog()->getEffectiveBoolValue("EmuCore/GS", "EnableVideoCapture", true);
	m_capture.videoCaptureOptions->setEnabled(enabled);
}

void GraphicsSettingsWidget::onOsdShowSettingsToggled()
{
	const bool enabled = dialog()->getEffectiveBoolValue("EmuCore/GS", "OsdShowSettings", false);
	m_osd.showPatches->setEnabled(enabled);
}

void GraphicsSettingsWidget::onEnableVideoCaptureArgumentsChanged()
{
	const bool enabled = dialog()->getEffectiveBoolValue("EmuCore/GS", "EnableVideoCaptureParameters", false);
	m_capture.videoCaptureArguments->setEnabled(enabled);
}

void GraphicsSettingsWidget::onVideoCaptureAutoResolutionChanged()
{
	const bool enabled = !dialog()->getEffectiveBoolValue("EmuCore/GS", "VideoCaptureAutoResolution", true);
	m_capture.videoCaptureWidth->setEnabled(enabled);
	m_capture.videoCaptureHeight->setEnabled(enabled);
}

void GraphicsSettingsWidget::onEnableAudioCaptureChanged()
{
	const bool enabled = dialog()->getEffectiveBoolValue("EmuCore/GS", "EnableAudioCapture", true);
	m_capture.audioCaptureOptions->setEnabled(enabled);
}

void GraphicsSettingsWidget::onEnableAudioCaptureArgumentsChanged()
{
	const bool enabled = dialog()->getEffectiveBoolValue("EmuCore/GS", "EnableAudioCaptureParameters", false);
	m_capture.audioCaptureArguments->setEnabled(enabled);
}

void GraphicsSettingsWidget::onGpuPaletteConversionChanged(int state)
{
	const bool disabled =
		state == Qt::CheckState::PartiallyChecked ? Host::GetBaseBoolSettingValue("EmuCore/GS", "paltex", false) : (state != 0);

	m_hw.anisotropicFiltering->setDisabled(disabled);
}

void GraphicsSettingsWidget::onCPUSpriteRenderBWChanged()
{
	const int value = dialog()->getEffectiveIntValue("EmuCore/GS", "UserHacks_CPUSpriteRenderBW", 0);
	m_fixes.cpuSpriteRenderLevel->setEnabled(value != 0);
}

GSRendererType GraphicsSettingsWidget::getEffectiveRenderer() const
{
	const GSRendererType type =
		static_cast<GSRendererType>(dialog()->getEffectiveIntValue("EmuCore/GS", "Renderer", static_cast<int>(GSRendererType::Auto)));
	return (type == GSRendererType::Auto) ? GSUtil::GetPreferredRenderer() : type;
}

void GraphicsSettingsWidget::updateRendererDependentOptions()
{
	const GSRendererType type = getEffectiveRenderer();

#ifdef _WIN32
	const bool is_dx11 = (type == GSRendererType::DX11 || type == GSRendererType::SW);
	const bool is_sw_dx = (type == GSRendererType::DX11 || type == GSRendererType::DX12 || type == GSRendererType::SW);
#else
	const bool is_dx11 = false;
	const bool is_sw_dx = false;
#endif

	const bool is_hardware = (type == GSRendererType::DX11 || type == GSRendererType::DX12 || type == GSRendererType::OGL ||
							  type == GSRendererType::VK || type == GSRendererType::Metal);
	const bool is_software = (type == GSRendererType::SW);
	const bool is_auto = (type == GSRendererType::Auto);
	const bool is_vk = (type == GSRendererType::VK);
	const bool is_disable_barriers = (type == GSRendererType::Metal || type == GSRendererType::SW);
	const bool hw_fixes = (is_hardware && m_hw.enableHWFixes && m_hw.enableHWFixes->checkState() == Qt::Checked);

	QWidget* prev_tab;
	if (is_hardware)
	{
		setTabVisible(m_hardware_rendering_tab, true);
		setTabVisible(m_software_rendering_tab, false, m_hardware_rendering_tab);

		prev_tab = m_hardware_rendering_tab;
	}
	else if (is_software)
	{
		setTabVisible(m_software_rendering_tab, true);
		setTabVisible(m_hardware_rendering_tab, false, m_software_rendering_tab);

		prev_tab = m_software_rendering_tab;
	}
	else
	{
		setTabVisible(m_hardware_rendering_tab, false, m_display_tab);
		setTabVisible(m_software_rendering_tab, false, m_display_tab);

		prev_tab = m_display_tab;
	}

	setTabVisible(m_hardware_fixes_tab, hw_fixes, prev_tab);
	setTabVisible(m_upscaling_fixes_tab, hw_fixes, prev_tab);
	setTabVisible(m_texture_replacement_tab, is_hardware, prev_tab);

	if (m_advanced.useBlitSwapChain)
		m_advanced.useBlitSwapChain->setEnabled(is_dx11);

	if (m_advanced.overrideTextureBarriers)
		m_advanced.overrideTextureBarriers->setDisabled(is_disable_barriers);

	if (m_advanced.disableFramebufferFetch)
		m_advanced.disableFramebufferFetch->setDisabled(is_sw_dx);

	if (m_advanced.exclusiveFullscreenControl)
		m_advanced.exclusiveFullscreenControl->setEnabled(is_auto || is_vk);

	// populate adapters
	std::vector<GSAdapterInfo> adapters = GSGetAdapterInfo(type);
	const GSAdapterInfo* current_adapter_info = nullptr;

	// fill+select adapters
	{
		QSignalBlocker sb(m_header.adapterDropdown);

		std::string current_adapter = Host::GetBaseStringSettingValue("EmuCore/GS", "Adapter", "");
		m_header.adapterDropdown->clear();
		m_header.adapterDropdown->setEnabled(!adapters.empty());
		m_header.adapterDropdown->addItem(tr("(Default)"));
		m_header.adapterDropdown->setCurrentIndex(0);

		// Treat default adapter as empty
		if (current_adapter == GetDefaultAdapter())
			current_adapter.clear();

		if (dialog()->isPerGameSettings())
		{
			m_header.adapterDropdown->insertItem(
				0, tr("Use Global Setting [%1]").arg((current_adapter.empty()) ? tr("(Default)") : QString::fromStdString(current_adapter)));
			if (!dialog()->getSettingsInterface()->GetStringValue("EmuCore/GS", "Adapter", &current_adapter))
			{
				// clear the adapter so we don't set it to the global value
				current_adapter.clear();
				m_header.adapterDropdown->setCurrentIndex(0);
			}
		}

		for (const GSAdapterInfo& adapter : adapters)
		{
			m_header.adapterDropdown->addItem(QString::fromStdString(adapter.name));
			if (current_adapter == adapter.name)
			{
				m_header.adapterDropdown->setCurrentIndex(m_header.adapterDropdown->count() - 1);
				current_adapter_info = &adapter;
			}
		}

		current_adapter_info = (current_adapter_info || adapters.empty()) ? current_adapter_info : &adapters.front();
	}

	// fill+select fullscreen modes
	{
		QSignalBlocker sb(m_display.fullscreenModes);

		std::string current_mode(Host::GetBaseStringSettingValue("EmuCore/GS", "FullscreenMode", ""));
		m_display.fullscreenModes->clear();
		m_display.fullscreenModes->addItem(tr("Borderless Fullscreen"));
		m_display.fullscreenModes->setCurrentIndex(0);

		if (dialog()->isPerGameSettings())
		{
			m_display.fullscreenModes->insertItem(
				0, tr("Use Global Setting [%1]").arg(current_mode.empty() ? tr("Borderless Fullscreen") : QString::fromStdString(current_mode)));
			if (!dialog()->getSettingsInterface()->GetStringValue("EmuCore/GS", "FullscreenMode", &current_mode))
			{
				current_mode.clear();
				m_display.fullscreenModes->setCurrentIndex(0);
			}
		}

		if (current_adapter_info)
		{
			for (const std::string& fs_mode : current_adapter_info->fullscreen_modes)
			{
				m_display.fullscreenModes->addItem(QString::fromStdString(fs_mode));
				if (current_mode == fs_mode)
					m_display.fullscreenModes->setCurrentIndex(m_display.fullscreenModes->count() - 1);
			}
		}
	}

	// assume the GPU can do 10K textures.
	const u32 max_upscale_multiplier = std::max(current_adapter_info ? current_adapter_info->max_upscale_multiplier : 0u, 10u);
	populateUpscaleMultipliers(max_upscale_multiplier);
}

void GraphicsSettingsWidget::populateUpscaleMultipliers(u32 max_upscale_multiplier)
{
	static constexpr std::pair<const char*, float> templates[] = {
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "Native (PS2) (Default)"), 1.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "2x Native (~720px/HD)"), 2.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "3x Native (~1080px/FHD)"), 3.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "4x Native (~1440px/QHD)"), 4.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "5x Native (~1800px/QHD+)"), 5.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "6x Native (~2160px/4K UHD)"), 6.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "7x Native (~2520px)"), 7.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "8x Native (~2880px/5K UHD)"), 8.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "9x Native (~3240px)"), 9.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "10x Native (~3600px/6K UHD)"), 10.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "11x Native (~3960px)"), 11.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "12x Native (~4320px/8K UHD)"), 12.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "13x Native (~4680px)"), 13.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "14x Native (~5040px)"), 14.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "15x Native (~5400px)"), 15.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "16x Native (~5760px)"), 16.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "17x Native (~6120px)"), 17.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "18x Native (~6480px/12K UHD)"), 18.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "19x Native (~6840px)"), 19.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "20x Native (~7200px)"), 20.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "21x Native (~7560px)"), 21.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "22x Native (~7920px)"), 22.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "23x Native (~8280px)"), 23.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "24x Native (~8640px/16K UHD)"), 24.0f},
		{QT_TRANSLATE_NOOP("GraphicsSettingsWidget", "25x Native (~9000px)"), 25.0f},
	};
	static constexpr u32 max_template_multiplier = 25;

	// Limit the dropdown to 12x if we're not showing advanced settings. Save the noobs.
	static constexpr u32 max_non_advanced_multiplier = 12;

	QSignalBlocker sb(m_hw.upscaleMultiplier);
	m_hw.upscaleMultiplier->clear();

	const u32 max_shown_multiplier = m_advanced.extendedUpscales && m_advanced.extendedUpscales->checkState() == Qt::Checked ?
	                                     max_upscale_multiplier :
	                                     std::min(max_upscale_multiplier, max_non_advanced_multiplier);
	for (const auto& [name, value] : templates)
	{
		if (value > max_shown_multiplier)
			break;

		m_hw.upscaleMultiplier->addItem(tr(name), QVariant(value));
	}
	for (u32 i = max_template_multiplier + 1; i <= max_shown_multiplier; i++)
		m_hw.upscaleMultiplier->addItem(tr("%1x Native").arg(i), QVariant(static_cast<float>(i)));

	const float global_value = Host::GetBaseFloatSettingValue("EmuCore/GS", "upscale_multiplier", 1.0f);
	if (dialog()->isPerGameSettings())
	{
		const int name_idx = m_hw.upscaleMultiplier->findData(QVariant(global_value));
		const QString global_name = (name_idx >= 0) ? m_hw.upscaleMultiplier->itemText(name_idx) : tr("%1x Native");
		m_hw.upscaleMultiplier->insertItem(0, tr("Use Global Setting [%1]").arg(global_name));

		const std::optional<float> config_value = dialog()->getFloatValue("EmuCore/GS", "upscale_multiplier", std::nullopt);
		if (config_value.has_value())
		{
			if (int index = m_hw.upscaleMultiplier->findData(QVariant(config_value.value())); index > 0)
				m_hw.upscaleMultiplier->setCurrentIndex(index);
		}
		else
		{
			m_hw.upscaleMultiplier->setCurrentIndex(0);
		}
	}
	else
	{
		if (int index = m_hw.upscaleMultiplier->findData(QVariant(global_value)); index > 0)
			m_hw.upscaleMultiplier->setCurrentIndex(index);
	}
}

void GraphicsSettingsWidget::onUpscaleMultiplierChanged()
{
	const QVariant data = m_hw.upscaleMultiplier->currentData();
	dialog()->setFloatSettingValue("EmuCore/GS", "upscale_multiplier",
		data.isValid() ? std::optional<float>(data.toFloat()) : std::optional<float>());
}

void GraphicsSettingsWidget::onUiDepthChanged()
{
    m_hw.stereoUiDepthValue->setText(QString::number(static_cast<int>(m_hw.stereoUiDepth->value())));
}

void GraphicsSettingsWidget::onUiSecondLayerDepthChanged()
{
	m_hw.stereoUiSecondLayerDepthValue->setText(QString::number(static_cast<int>(m_hw.stereoUiSecondLayerDepth->value())));
}
