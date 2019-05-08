/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CClientPlayerVoice.h
 *  PURPOSE:     Remote player voice chat playback
 *
 *  Multi Theft Auto is available from http://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <opus.h>
#include <CClientPlayer.h>
#include <../deathmatch/CVoiceRecorder.h>
#include <bass.h>

#define VOICE_DEBUG_LOCAL_PLAYBACK

class CClientPlayerVoice
{
public:
    ZERO_ON_NEW
    CClientPlayerVoice(CClientPlayer* pPlayer, CVoiceRecorder* pVoiceRecorder);
    ~CClientPlayerVoice();
    void DecodeAndBuffer(unsigned char* pBuffer, unsigned int bytesWritten);
    void VoiceStateChange(bool bState);
    void DoPulse();

    bool m_bVoiceActive;

    void GetTempoValues(float& fSampleRate, float& fTempo, float& fPitch, bool& bReverse)
    {
        fSampleRate = m_fSampleRate;
        fTempo = m_fTempo;
        fPitch = m_fPitch;
        bReverse = false;
    };

    void SetPaused(bool bPaused);
    bool IsPaused();

    void   SetPlayPosition(double dPosition);
    double GetPlayPosition();

    double GetLength(bool bAvoidLoad = false);

    void  SetVolume(float fVolume, bool bStore = true);
    float GetVolume();

    void  SetPlaybackSpeed(float fSpeed);
    float GetPlaybackSpeed();

    void   ApplyFXModifications(float fSampleRate, float fTempo, float fPitch, bool bReversed);
    void   GetFXModifications(float& fSampleRate, float& fTempo, float& fPitch, bool& bReversed);
    float* GetFFTData(int iLength);
    float* GetWaveData(int iLength);
    bool   IsPanEnabled();
    bool   SetPanEnabled(bool bPan);
    DWORD  GetLevelData();
    float  GetSoundBPM();

    bool SetPan(float fPan);
    bool GetPan(float& fPan);

    bool SetFxEffect(uint uiFxEffect, bool bEnable);
    bool IsFxEffectEnabled(uint uiFxEffect);
    bool IsActive() { return m_bVoiceActive; }

private:
    void Init();
    void DeInit();
    void ApplyFxEffects();

    CClientPlayer*  m_pPlayer;
    CVoiceRecorder* m_pVoiceRecorder;
    unsigned int    m_SampleRate;
    unsigned char   m_Channels;
    HSTREAM         m_pBassPlaybackStream;
    OpusDecoder*    m_pOpusDecoder;
    float           m_fVolume;
    float           m_fVolumeScale;

    // Playback altering stuff
    float m_fPitch;
    float m_fTempo;
    float m_fSampleRate;
    bool  m_bPan;
    float m_fPan;

    // Playback state
    bool  m_bPaused;
    float m_fPlaybackSpeed;
    float m_fDefaultFrequency;

    SFixedArray<int, 9> m_EnabledEffects;
    SFixedArray<HFX, 9> m_FxEffects;
};
