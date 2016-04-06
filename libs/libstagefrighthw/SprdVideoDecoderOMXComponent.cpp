/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <inttypes.h>

#define LOG_NDEBUG 0
#define LOG_TAG "SprdVideoDecoderOMXComponent"
#include <utils/Log.h>

#include <SprdVideoDecoderOMXComponent.h>

#include <media/hardware/HardwareAPI.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/AUtils.h>
#include <media/stagefright/MediaDefs.h>

namespace android {

template<class T>
static void InitOMXParams(T *params) {
    params->nSize = sizeof(T);
    params->nVersion.s.nVersionMajor = 1;
    params->nVersion.s.nVersionMinor = 0;
    params->nVersion.s.nRevision = 0;
    params->nVersion.s.nStep = 0;
}

SprdVideoDecoderOMXComponent::SprdVideoDecoderOMXComponent(
        const char *name,
        const char *componentRole,
        OMX_VIDEO_CODINGTYPE codingType,
        const CodecProfileLevel *profileLevels,
        size_t numProfileLevels,
        int32_t width,
        int32_t height,
        const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData,
        OMX_COMPONENTTYPE **component)
        : SprdSimpleOMXComponent(name, callbacks, appData, component),
        mIsAdaptive(false),
        mAdaptiveMaxWidth(0),
        mAdaptiveMaxHeight(0),
        mWidth(width),
        mHeight(height),
        mCropLeft(0),
        mCropTop(0),
        mCropWidth(width),
        mCropHeight(height),
        mOutputPortSettingsChange(NONE),
        mMinInputBufferSize(384), // arbitrary, using one uncompressed macroblock
        mMinCompressionRatio(1),  // max input size is normally the output size
        mComponentRole(componentRole),
        mCodingType(codingType),
        mProfileLevels(profileLevels),
        mNumProfileLevels(numProfileLevels) {
}

void SprdVideoDecoderOMXComponent::initPorts(
        OMX_U32 numInputBuffers,
        OMX_U32 inputBufferSize,
        OMX_U32 numOutputBuffers,
        const char *mimeType,
        OMX_U32 minCompressionRatio) {
    mMinInputBufferSize = inputBufferSize;
    mMinCompressionRatio = minCompressionRatio;

    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);

    def.nPortIndex = kInputPortIndex;
    def.eDir = OMX_DirInput;
    def.nBufferCountMin = numInputBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = inputBufferSize;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainVideo;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 1;

    def.format.video.cMIMEType = const_cast<char *>(mimeType);
    def.format.video.pNativeRender = NULL;
    /* size is initialized in updatePortDefinitions() */
    def.format.video.nBitrate = 0;
    def.format.video.xFramerate = 0;
    def.format.video.bFlagErrorConcealment = OMX_FALSE;
    def.format.video.eCompressionFormat = mCodingType;
    def.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    def.format.video.pNativeWindow = NULL;

    addPort(def);

    def.nPortIndex = kOutputPortIndex;
    def.eDir = OMX_DirOutput;
    def.nBufferCountMin = numOutputBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainVideo;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 2;

    def.format.video.cMIMEType = const_cast<char *>("video/raw");
    def.format.video.pNativeRender = NULL;
    /* size is initialized in updatePortDefinitions() */
    def.format.video.nBitrate = 0;
    def.format.video.xFramerate = 0;
    def.format.video.bFlagErrorConcealment = OMX_FALSE;
    def.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;
    def.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
    def.format.video.pNativeWindow = NULL;

    addPort(def);

    updatePortDefinitions(true /* updateCrop */, true /* updateInputSize */);
}

void SprdVideoDecoderOMXComponent::updatePortDefinitions(bool updateCrop, bool updateInputSize) {
    OMX_PARAM_PORTDEFINITIONTYPE *outDef = &editPortInfo(kOutputPortIndex)->mDef;
    outDef->format.video.nFrameWidth = outputBufferWidth();
    outDef->format.video.nFrameHeight = outputBufferHeight();
    outDef->format.video.nStride = outDef->format.video.nFrameWidth;
    outDef->format.video.nSliceHeight = outDef->format.video.nFrameHeight;

    outDef->nBufferSize =
        (outDef->format.video.nStride * outDef->format.video.nSliceHeight * 3) / 2;

    OMX_PARAM_PORTDEFINITIONTYPE *inDef = &editPortInfo(kInputPortIndex)->mDef;
    inDef->format.video.nFrameWidth = mWidth;
    inDef->format.video.nFrameHeight = mHeight;
    // input port is compressed, hence it has no stride
    inDef->format.video.nStride = 0;
    inDef->format.video.nSliceHeight = 0;

    // when output format changes, input buffer size does not actually change
    if (updateInputSize) {
        inDef->nBufferSize = max(
                outDef->nBufferSize / mMinCompressionRatio,
                max(mMinInputBufferSize, inDef->nBufferSize));
    }

    if (updateCrop) {
        mCropLeft = 0;
        mCropTop = 0;
        mCropWidth = mWidth;
        mCropHeight = mHeight;
    }
}


uint32_t SprdVideoDecoderOMXComponent::outputBufferWidth() {
    return mIsAdaptive ? mAdaptiveMaxWidth : mWidth;
}

uint32_t SprdVideoDecoderOMXComponent::outputBufferHeight() {
    return mIsAdaptive ? mAdaptiveMaxHeight : mHeight;
}

void SprdVideoDecoderOMXComponent::handlePortSettingsChange(
        bool *portWillReset, uint32_t width, uint32_t height,
        CropSettingsMode cropSettingsMode, bool fakeStride) {
    *portWillReset = false;
    bool sizeChanged = (width != mWidth || height != mHeight);
    bool updateCrop = (cropSettingsMode == kCropUnSet);
    bool cropChanged = (cropSettingsMode == kCropChanged);
    bool strideChanged = false;
    if (fakeStride) {
        OMX_PARAM_PORTDEFINITIONTYPE *def = &editPortInfo(kOutputPortIndex)->mDef;
        if (def->format.video.nStride != (OMX_S32)width
                || def->format.video.nSliceHeight != (OMX_U32)height) {
            strideChanged = true;
        }
    }

    if (sizeChanged || cropChanged || strideChanged) {
        mWidth = width;
        mHeight = height;

        if ((sizeChanged && !mIsAdaptive)
            || width > mAdaptiveMaxWidth
            || height > mAdaptiveMaxHeight) {
            if (mIsAdaptive) {
                if (width > mAdaptiveMaxWidth) {
                    mAdaptiveMaxWidth = width;
                }
                if (height > mAdaptiveMaxHeight) {
                    mAdaptiveMaxHeight = height;
                }
            }
            updatePortDefinitions(updateCrop);
            notify(OMX_EventPortSettingsChanged, kOutputPortIndex, 0, NULL);
            mOutputPortSettingsChange = AWAITING_DISABLED;
            *portWillReset = true;
        } else {
            updatePortDefinitions(updateCrop);

            if (fakeStride) {
                // MAJOR HACK that is not pretty, it's just to fool the renderer to read the correct
                // data.
                // Some software decoders (e.g. SoftMPEG4) fill decoded frame directly to output
                // buffer without considering the output buffer stride and slice height. So this is
                // used to signal how the buffer is arranged.  The alternative is to re-arrange the
                // output buffer in SoftMPEG4, but that results in memcopies.
                OMX_PARAM_PORTDEFINITIONTYPE *def = &editPortInfo(kOutputPortIndex)->mDef;
                def->format.video.nStride = mWidth;
                def->format.video.nSliceHeight = mHeight;
            }

            notify(OMX_EventPortSettingsChanged, kOutputPortIndex,
                   OMX_IndexConfigCommonOutputCrop, NULL);
        }
    }
}

OMX_ERRORTYPE SprdVideoDecoderOMXComponent::internalGetParameter(
        OMX_INDEXTYPE index, OMX_PTR params) {
    switch (index) {
    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *formatParams =
            (OMX_VIDEO_PARAM_PORTFORMATTYPE *)params;

        if (formatParams->nPortIndex > kMaxPortIndex) {
            return OMX_ErrorBadPortIndex;
        }

        if (formatParams->nIndex != 0) {
            return OMX_ErrorNoMore;
        }

        if (formatParams->nPortIndex == kInputPortIndex) {
            formatParams->eCompressionFormat = mCodingType;
            formatParams->eColorFormat = OMX_COLOR_FormatUnused;
            formatParams->xFramerate = 0;
        } else {
            CHECK_EQ(formatParams->nPortIndex, kOutputPortIndex);

            PortInfo *pOutPort = editPortInfo(OMX_DirOutput);
            ALOGI("internalGetParameter, OMX_IndexParamVideoPortFormat, eColorFormat: 0x%x", pOutPort->mDef.format.video.eColorFormat);
            formatParams->eCompressionFormat = OMX_VIDEO_CodingUnused;
            formatParams->eColorFormat = pOutPort->mDef.format.video.eColorFormat;
            formatParams->xFramerate = 0;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoProfileLevelQuerySupported:
    {
        OMX_VIDEO_PARAM_PROFILELEVELTYPE *profileLevel =
              (OMX_VIDEO_PARAM_PROFILELEVELTYPE *) params;

        if (profileLevel->nPortIndex != kInputPortIndex) {
            ALOGE("Invalid port index: %" PRIu32, profileLevel->nPortIndex);
            return OMX_ErrorUnsupportedIndex;
        }

        if (profileLevel->nProfileIndex >= mNumProfileLevels) {
            return OMX_ErrorNoMore;
        }

        profileLevel->eProfile = mProfileLevels[profileLevel->nProfileIndex].mProfile;
        profileLevel->eLevel   = mProfileLevels[profileLevel->nProfileIndex].mLevel;
        return OMX_ErrorNone;
    }

    case OMX_IndexParamEnableAndroidBuffers:
    {
        EnableAndroidNativeBuffersParams *peanbp = (EnableAndroidNativeBuffersParams *)params;
        peanbp->enable = iUseAndroidNativeBuffer[OMX_DirOutput];
        ALOGI("internalGetParameter, OMX_IndexParamEnableAndroidBuffers %d",peanbp->enable);
        return OMX_ErrorNone;
    }

    case OMX_IndexParamGetAndroidNativeBuffer:
    {
        GetAndroidNativeBufferUsageParams *pganbp;

        pganbp = (GetAndroidNativeBufferUsageParams *)params;
        if(mDecoderSwFlag || mIOMMUEnabled) {
            pganbp->nUsage = GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN;
        } else {
            pganbp->nUsage = GRALLOC_USAGE_VIDEO_BUFFER | GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN;
        }
        ALOGI("internalGetParameter, OMX_IndexParamGetAndroidNativeBuffer %x",pganbp->nUsage);
        return OMX_ErrorNone;
    }

    default:
        return SprdSimpleOMXComponent::internalGetParameter(index, params);
    }
}

OMX_ERRORTYPE SprdVideoDecoderOMXComponent::internalSetParameter(
        OMX_INDEXTYPE index, const OMX_PTR params)
{
    // Include extension index OMX_INDEXEXTTYPE.
    const int32_t indexFull = index;

    switch (indexFull) {
    case OMX_IndexParamStandardComponentRole:
    {
        const OMX_PARAM_COMPONENTROLETYPE *roleParams =
            (const OMX_PARAM_COMPONENTROLETYPE *)params;

        if (strncmp((const char *)roleParams->cRole,
                    mComponentRole,
                    OMX_MAX_STRINGNAME_SIZE - 1)) {
            return OMX_ErrorUndefined;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *formatParams =
            (OMX_VIDEO_PARAM_PORTFORMATTYPE *)params;

        if (formatParams->nPortIndex > kMaxPortIndex) {
            return OMX_ErrorBadPortIndex;
        }

        if (formatParams->nIndex != 0) {
            return OMX_ErrorNoMore;
        }

        if (formatParams->nPortIndex == kInputPortIndex) {
            if (formatParams->eCompressionFormat != mCodingType
                    || formatParams->eColorFormat != OMX_COLOR_FormatUnused) {
                return OMX_ErrorUnsupportedSetting;
            }
        } else {
            if (formatParams->eCompressionFormat != OMX_VIDEO_CodingUnused
                    || formatParams->eColorFormat != OMX_COLOR_FormatYUV420SemiPlanar) {
                return OMX_ErrorUnsupportedSetting;
            }
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamEnableAndroidBuffers:
    {
        EnableAndroidNativeBuffersParams *peanbp = (EnableAndroidNativeBuffersParams *)params;
        PortInfo *pOutPort = editPortInfo(1);
        if (peanbp->enable == OMX_FALSE) {
            ALOGI("internalSetParameter, disable AndroidNativeBuffer");
            iUseAndroidNativeBuffer[OMX_DirOutput] = OMX_FALSE;

            pOutPort->mDef.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
        } else {
            ALOGI("internalSetParameter, enable AndroidNativeBuffer");
            iUseAndroidNativeBuffer[OMX_DirOutput] = OMX_TRUE;

            pOutPort->mDef.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
        }
        return OMX_ErrorNone;
    }

    case kPrepareForAdaptivePlaybackIndex:
    {
        const PrepareForAdaptivePlaybackParams* adaptivePlaybackParams =
                (const PrepareForAdaptivePlaybackParams *)params;
        mIsAdaptive = adaptivePlaybackParams->bEnable;
        if (mIsAdaptive) {
            ALOGI("internalSetParameter, enable AdaptivePlayback");
            mAdaptiveMaxWidth = adaptivePlaybackParams->nMaxFrameWidth;
            mAdaptiveMaxHeight = adaptivePlaybackParams->nMaxFrameHeight;
            mWidth = mAdaptiveMaxWidth;
            mHeight = mAdaptiveMaxHeight;
        } else {
            mAdaptiveMaxWidth = 0;
            mAdaptiveMaxHeight = 0;
        }
        updatePortDefinitions(true /* updateCrop */, true /* updateInputSize */);
        return OMX_ErrorNone;
    }

    case OMX_IndexParamPortDefinition:
    {
        OMX_PARAM_PORTDEFINITIONTYPE *newParams =
            (OMX_PARAM_PORTDEFINITIONTYPE *)params;
        OMX_VIDEO_PORTDEFINITIONTYPE *video_def = &newParams->format.video;
        OMX_PARAM_PORTDEFINITIONTYPE *def = &editPortInfo(newParams->nPortIndex)->mDef;

        uint32_t oldWidth = def->format.video.nFrameWidth;
        uint32_t oldHeight = def->format.video.nFrameHeight;
        uint32_t newWidth = video_def->nFrameWidth;
        uint32_t newHeight = video_def->nFrameHeight;
        if (newWidth != oldWidth || newHeight != oldHeight) {
            bool outputPort = (newParams->nPortIndex == kOutputPortIndex);
            if (outputPort) {
                // only update (essentially crop) if size changes
                mWidth = newWidth;
                mHeight = newHeight;

                updatePortDefinitions(true /* updateCrop */, true /* updateInputSize */);
                // reset buffer size based on frame size
                newParams->nBufferSize = def->nBufferSize;
            } else {
                // For input port, we only set nFrameWidth and nFrameHeight. Buffer size
                // is updated when configuring the output port using the max-frame-size,
                // though client can still request a larger size.
                def->format.video.nFrameWidth = newWidth;
                def->format.video.nFrameHeight = newHeight;
            }
        }
        return SprdSimpleOMXComponent::internalSetParameter(index, params);
    }

    default:
        return SprdSimpleOMXComponent::internalSetParameter(index, params);
    }
}

OMX_ERRORTYPE SprdVideoDecoderOMXComponent::getConfig(
        OMX_INDEXTYPE index, OMX_PTR params) {
    switch (index) {
    case OMX_IndexConfigCommonOutputCrop: {
        OMX_CONFIG_RECTTYPE *rectParams = (OMX_CONFIG_RECTTYPE *)params;

        if (rectParams->nPortIndex != kOutputPortIndex) {
            return OMX_ErrorUndefined;
        }

        rectParams->nLeft = mCropLeft;
        rectParams->nTop = mCropTop;
        rectParams->nWidth = mCropWidth;
        rectParams->nHeight = mCropHeight;

        return OMX_ErrorNone;
    }

    default:
            return OMX_ErrorUnsupportedIndex;
    }
}

OMX_ERRORTYPE SprdVideoDecoderOMXComponent::getExtensionIndex(
        const char *name, OMX_INDEXTYPE *index) {
    ALOGI("getExtensionIndex, name: %s",name);
    if (!strcmp(name, "OMX.google.android.index.prepareForAdaptivePlayback")) {
        *(int32_t*)index = kPrepareForAdaptivePlaybackIndex;
        return OMX_ErrorNone;
    } else if(strcmp(name, SPRD_INDEX_PARAM_ENABLE_ANB) == 0) {
        ALOGI("getExtensionIndex: %s", SPRD_INDEX_PARAM_ENABLE_ANB);
        *index = (OMX_INDEXTYPE) OMX_IndexParamEnableAndroidBuffers;
        return OMX_ErrorNone;
    } else if (strcmp(name, SPRD_INDEX_PARAM_GET_ANB) == 0) {
        ALOGI("getExtensionIndex: %s", SPRD_INDEX_PARAM_GET_ANB);
        *index = (OMX_INDEXTYPE) OMX_IndexParamGetAndroidNativeBuffer;
        return OMX_ErrorNone;
    }	else if (strcmp(name, SPRD_INDEX_PARAM_USE_ANB) == 0) {
        ALOGI("getExtensionIndex: %s", SPRD_INDEX_PARAM_USE_ANB);
        *index = OMX_IndexParamUseAndroidNativeBuffer2;
        return OMX_ErrorNone;
    }
    return SprdSimpleOMXComponent::getExtensionIndex(name, index);
}

void SprdVideoDecoderOMXComponent::onReset() {
    mOutputPortSettingsChange = NONE;
}

void SprdVideoDecoderOMXComponent::onPortEnableCompleted(OMX_U32 portIndex, bool enabled)
{
    if (portIndex != kOutputPortIndex) {
        return;
    }
    switch (mOutputPortSettingsChange) {
    case NONE:
        break;
    case AWAITING_DISABLED: {
        CHECK(!enabled);
        mOutputPortSettingsChange = AWAITING_ENABLED;
        break;
    }
    default: {
        CHECK_EQ((int)mOutputPortSettingsChange, (int)AWAITING_ENABLED);
        CHECK(enabled);
        mOutputPortSettingsChange = NONE;
        break;
    }
    }
}

}  // namespace android
