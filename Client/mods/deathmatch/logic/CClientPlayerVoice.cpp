/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CClientPlayerVoice.cpp
 *  PURPOSE:     Remote player voice chat playback
 *
 *  Multi Theft Auto is available from http://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CClientPlayerVoice.h"
#include "CBassAudio.h"
#include <process.h>
#include <tags.h>
#include <bassmix.h>
#include <basswma.h>
#include <bass_fx.h>

void CALLBACK BPMCallback(int handle, float bpm, void* user);
void CALLBACK BeatCallback(DWORD chan, double beatpos, void* user);

#define INVALID_FX_HANDLE (-1)            // Hope that BASS doesn't use this as a valid Fx handle

CClientPlayerVoice::CClientPlayerVoice(CClientPlayer* pPlayer, CVoiceRecorder* pVoiceRecorder)
{
    m_pPlayer = pPlayer;
    m_pVoiceRecorder = pVoiceRecorder;
    m_bVoiceActive = false;
    m_SampleRate = SAMPLERATE_WIDEBAND;
    m_Channels = 2;
    m_pOpusDecoder = nullptr;

    // Get initial voice volume
    m_fVolume = 1.0f;
    g_pCore->GetCVars()->Get("voicevolume", m_fVolumeScale);
    m_fVolumeScale *= g_pCore->GetCVars()->GetValue<float>("mastervolume", 1.0f);

    m_fVolume = m_fVolume * m_fVolumeScale;

#ifndef VOICE_DEBUG_LOCAL_PLAYBACK
    if (pPlayer->IsLocalPlayer() == true)
    {
        m_fVolume = 0.0f;
    }
#endif
    m_fPlaybackSpeed = 1.0f;
    Init();
}
CClientPlayerVoice::~CClientPlayerVoice()
{
    DeInit();
}

void CClientPlayerVoice::Init()
{
    // Grab our sample rate and number of channels
    m_SampleRate = m_pVoiceRecorder->GetSampleRate();
    m_Channels = m_pVoiceRecorder->GetChannels();

    // Setup our BASS playback device
    m_pBassPlaybackStream = BASS_StreamCreate(m_SampleRate, m_Channels, BASS_STREAM_AUTOFREE, STREAMPROC_PUSH, NULL);

    BASS_ChannelPlay(m_pBassPlaybackStream, false);
    BASS_ChannelSetAttribute(m_pBassPlaybackStream, BASS_ATTRIB_VOL, m_fVolume * m_fVolumeScale);

    int err;
    m_pOpusDecoder = opus_decoder_create(m_SampleRate, m_Channels, &err);
    if (err != OPUS_OK)
    {
        g_pCore->GetConsole()->Printf("[VOICE] Failed to create decoder for %s's voice data.", m_pPlayer->GetNick());
        m_pOpusDecoder = nullptr;
    }
}

void CClientPlayerVoice::DeInit()
{
    BASS_ChannelStop(m_pBassPlaybackStream);
    BASS_StreamFree(m_pBassPlaybackStream);

    m_Channels = 2;

    m_pBassPlaybackStream = NULL;

    if (m_pOpusDecoder != nullptr)
        opus_decoder_destroy(m_pOpusDecoder);
    m_pOpusDecoder = nullptr;

    m_SampleRate = SAMPLERATE_WIDEBAND;
}

void CClientPlayerVoice::DoPulse()
{
    float fPreviousVolume = 0.0f;
    g_pCore->GetCVars()->Get("voicevolume", fPreviousVolume);
    fPreviousVolume *= g_pCore->GetCVars()->GetValue<float>("mastervolume", 1.0f);

    if (fPreviousVolume != m_fVolumeScale
#ifndef VOICE_DEBUG_LOCAL_PLAYBACK
        && m_pPlayer->IsLocalPlayer() == false
#endif
    )
    {
        m_fVolumeScale = fPreviousVolume;
        float fScaledVolume = m_fVolume * m_fVolumeScale;
        BASS_ChannelSetAttribute(m_pBassPlaybackStream, BASS_ATTRIB_VOL, fScaledVolume);
    }
}

void CClientPlayerVoice::DecodeAndBuffer(unsigned char* pBuffer, unsigned int bytesWritten)
{
    VoiceStateChange(true);

    if (m_bVoiceActive && m_pOpusDecoder != nullptr)            // If we reach here and m_bVoiceActive isn't true, then voice start must've been cancelled
    {
        opus_int16   pTempBuffer[VOICE_MAX_FRAME_SIZE];
        unsigned int uiLength = opus_decode(m_pOpusDecoder, pBuffer, bytesWritten, pTempBuffer, VOICE_MAX_FRAME_SIZE / 2, 0);

        if (uiLength >= 0)
            BASS_StreamPutData(m_pBassPlaybackStream, (void*)pTempBuffer, uiLength * m_Channels * sizeof(opus_int16));
    }
}

void CClientPlayerVoice::VoiceStateChange(bool bState)
{
    if (bState)
    {
        if (m_bVoiceActive != bState)
        {
            CLuaArguments Arguments;
            if (!m_pPlayer->CallEvent("onClientPlayerVoiceStart", Arguments, true))
                return;
        }
    }
    else
    {
        if (m_bVoiceActive != bState)
        {
            CLuaArguments Arguments;
            m_pPlayer->CallEvent("onClientPlayerVoiceStop", Arguments, true);
        }
    }

    m_bVoiceActive = bState;
}

////////////////////////////////////////////////////////////
//
// CClientPlayerVoice:: Sea of sets 'n' gets
//
//
//
////////////////////////////////////////////////////////////
void CClientPlayerVoice::SetPlayPosition(double dPosition)
{
    // Only relevant for non-streams, which are always ready if valid
    if (m_pBassPlaybackStream)
    {
        // Make sure position is in range
        QWORD bytePosition = BASS_ChannelSeconds2Bytes(m_pBassPlaybackStream, dPosition);
        QWORD byteLength = BASS_ChannelGetLength(m_pBassPlaybackStream, BASS_POS_BYTE);
        BASS_ChannelSetPosition(m_pBassPlaybackStream, Clamp<QWORD>(0, bytePosition, byteLength - 1), BASS_POS_BYTE);
    }
}

double CClientPlayerVoice::GetPlayPosition()
{
    if (m_pBassPlaybackStream)
    {
        QWORD pos = BASS_ChannelGetPosition(m_pBassPlaybackStream, BASS_POS_BYTE);
        if (pos != -1)
            return BASS_ChannelBytes2Seconds(m_pBassPlaybackStream, pos);
    }
    return 0.0;
}

double CClientPlayerVoice::GetLength(bool bAvoidLoad)
{
    if (m_pBassPlaybackStream)
    {
        QWORD length = BASS_ChannelGetLength(m_pBassPlaybackStream, BASS_POS_BYTE);
        if (length != -1)
            return BASS_ChannelBytes2Seconds(m_pBassPlaybackStream, length);
    }
    return 0;
}

float CClientPlayerVoice::GetVolume()
{
    return m_fVolume;
}

void CClientPlayerVoice::SetVolume(float fVolume, bool bStore)
{
    m_fVolume = fVolume;

    if (m_pBassPlaybackStream
#ifndef VOICE_DEBUG_LOCAL_PLAYBACK
        && m_pPlayer->IsLocalPlayer() == false
#endif
    )
    {
        float fScaledVolume = m_fVolume * m_fVolumeScale;
        BASS_ChannelSetAttribute(m_pBassPlaybackStream, BASS_ATTRIB_VOL, fScaledVolume);
    }
}

float CClientPlayerVoice::GetPlaybackSpeed()
{
    return m_fPlaybackSpeed;
}

void CClientPlayerVoice::SetPlaybackSpeed(float fSpeed)
{
    m_fPlaybackSpeed = fSpeed;

    if (m_pBassPlaybackStream)
        BASS_ChannelSetAttribute(m_pBassPlaybackStream, BASS_ATTRIB_FREQ, fSpeed * m_fDefaultFrequency);
}

void CClientPlayerVoice::ApplyFXModifications(float fSampleRate, float fTempo, float fPitch, bool bReversed)
{
    m_fSampleRate = fSampleRate;
    m_fTempo = fTempo;
    m_fPitch = fPitch;
    if (m_pBassPlaybackStream)
    {
        if (fTempo != m_fTempo)
        {
            m_fTempo = fTempo;
        }
        if (fPitch != m_fPitch)
        {
            m_fPitch = fPitch;
        }
        if (fSampleRate != m_fSampleRate)
        {
            m_fSampleRate = fSampleRate;
        }

        // Update our attributes
        BASS_ChannelSetAttribute(m_pBassPlaybackStream, BASS_ATTRIB_TEMPO, m_fTempo);
        BASS_ChannelSetAttribute(m_pBassPlaybackStream, BASS_ATTRIB_TEMPO_PITCH, m_fPitch);
        BASS_ChannelSetAttribute(m_pBassPlaybackStream, BASS_ATTRIB_TEMPO_FREQ, m_fSampleRate);
    }
}

void CClientPlayerVoice::GetFXModifications(float& fSampleRate, float& fTempo, float& fPitch, bool& bReversed)
{
    if (m_pBassPlaybackStream)
    {
        GetTempoValues(fSampleRate, fTempo, fPitch, bReversed);
    }
}

float* CClientPlayerVoice::GetFFTData(int iLength)
{
    if (m_pBassPlaybackStream)
    {
        long lFlags = BASS_DATA_FFT256;
        if (iLength == 256)
            lFlags = BASS_DATA_FFT256;
        else if (iLength == 512)
            lFlags = BASS_DATA_FFT512;
        else if (iLength == 1024)
            lFlags = BASS_DATA_FFT1024;
        else if (iLength == 2048)
            lFlags = BASS_DATA_FFT2048;
        else if (iLength == 4096)
            lFlags = BASS_DATA_FFT4096;
        else if (iLength == 8192)
            lFlags = BASS_DATA_FFT8192;
        else if (iLength == 16384)
            lFlags = BASS_DATA_FFT16384;
        else
            return NULL;

        float* pData = new float[iLength];
        if (BASS_ChannelGetData(m_pBassPlaybackStream, pData, lFlags) != -1)
            return pData;
        else
        {
            delete[] pData;
            return NULL;
        }
    }
    return NULL;
}

float* CClientPlayerVoice::GetWaveData(int iLength)
{
    if (m_pBassPlaybackStream)
    {
        long lFlags = 0;
        if (iLength == 128 || iLength == 256 || iLength == 512 || iLength == 1024 || iLength == 2048 || iLength == 4096 || iLength == 8192 || iLength == 16384)
        {
            lFlags = 4 * iLength | BASS_DATA_FLOAT;
        }
        else
            return NULL;

        float* pData = new float[iLength];
        if (BASS_ChannelGetData(m_pBassPlaybackStream, pData, lFlags) != -1)
            return pData;
        else
        {
            delete[] pData;
            return NULL;
        }
    }
    return NULL;
}

DWORD CClientPlayerVoice::GetLevelData()
{
    if (m_pBassPlaybackStream)
    {
        DWORD dwData = BASS_ChannelGetLevel(m_pBassPlaybackStream);
        if (dwData != 0)
            return dwData;
    }
    return 0;
}

////////////////////////////////////////////////////////////
//
// CClientSound::SetFxEffect
//
//
//
////////////////////////////////////////////////////////////
bool CClientPlayerVoice::SetFxEffect(uint uiFxEffect, bool bEnable)
{
    if (uiFxEffect >= NUMELMS(m_EnabledEffects))
        return false;

    m_EnabledEffects[uiFxEffect] = bEnable;

    // Apply if active
    if (m_pBassPlaybackStream)
        ApplyFxEffects();

    return true;
}

//
// Copy state stored in m_EnabledEffects to actual BASS sound
//
void CClientPlayerVoice::ApplyFxEffects()
{
    for (uint i = 0; i < NUMELMS(m_FxEffects) && NUMELMS(m_EnabledEffects); i++)
    {
        if (m_EnabledEffects[i] && !m_FxEffects[i])
        {
            // Switch on
            m_FxEffects[i] = BASS_ChannelSetFX(m_pBassPlaybackStream, i, 0);
            if (!m_FxEffects[i])
                m_FxEffects[i] = INVALID_FX_HANDLE;
        }
        else
        {
            if (!m_EnabledEffects[i] && m_FxEffects[i])
            {
                // Switch off
                if (m_FxEffects[i] != INVALID_FX_HANDLE)
                    BASS_ChannelRemoveFX(m_pBassPlaybackStream, m_FxEffects[i]);
                m_FxEffects[i] = 0;
            }
        }
    }
}

bool CClientPlayerVoice::IsFxEffectEnabled(uint uiFxEffect)
{
    if (uiFxEffect >= NUMELMS(m_EnabledEffects))
        return false;

    return m_EnabledEffects[uiFxEffect] ? true : false;
}

bool CClientPlayerVoice::GetPan(float& fPan)
{
    fPan = 0.0f;
    if (m_pBassPlaybackStream)
    {
        BASS_ChannelGetAttribute(m_pBassPlaybackStream, BASS_ATTRIB_PAN, &fPan);
        return true;
    }
    return false;
}

bool CClientPlayerVoice::SetPan(float fPan)
{
    if (m_pBassPlaybackStream)
    {
        BASS_ChannelSetAttribute(m_pBassPlaybackStream, BASS_ATTRIB_PAN, fPan);

        return true;
    }

    return false;
}

void CClientPlayerVoice::SetPaused(bool bPaused)
{
    if (m_bPaused != bPaused)
    {
        if (bPaused)
        {
            // call onClientPlayerVoicePause
            CLuaArguments Arguments;
            Arguments.PushString("paused");            // Reason
            m_pPlayer->CallEvent("onClientPlayerVoicePause", Arguments, false);
        }
        else
        {
            // call onClientPlayerVoiceResumed
            CLuaArguments Arguments;
            Arguments.PushString("resumed");            // Reason
            m_pPlayer->CallEvent("onClientPlayerVoiceResumed", Arguments, false);
        }
    }

    m_bPaused = bPaused;

    if (m_pBassPlaybackStream)
    {
        if (bPaused)
            BASS_ChannelPause(m_pBassPlaybackStream);
        else
            BASS_ChannelPlay(m_pBassPlaybackStream, false);
    }
}

bool CClientPlayerVoice::IsPaused()
{
    return m_bPaused;
}
