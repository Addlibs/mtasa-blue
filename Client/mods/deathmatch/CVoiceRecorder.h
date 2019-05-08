/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/CVoiceRecorder.h
 *  PURPOSE:     Player voice recording, encoding and sending
 *
 *  Multi Theft Auto is available from http://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <bass.h>
#include <opus.h>

#define VOICE_OUTGOING_FRAMES_COUNT (100)                  // Increase if frame dropping occurs too often
#define VOICE_MAX_PACKET_SIZE (1276)                       // As recommended on https://www.opus-codec.org/docs/html_api/group__opusencoder.html
#define VOICE_MAX_FRAME_SIZE ((48000 / 50) * 2)            // largest 20ms frame possible

enum eVoiceState
{
    VOICESTATE_AWAITING_INPUT = 0,
    VOICESTATE_RECORDING,
    VOICESTATE_RECORDING_LAST_PACKET,
};

enum eSampleRate
{
    SAMPLERATE_NARROWBAND = 8000,
    SAMPLERATE_MEDIUMBAND = 12000,
    SAMPLERATE_WIDEBAND = 16000,
    SAMPLERATE_SUPERWIDEBAND = 24000,
    SAMPLERATE_FULLBAND = 48000,
};

enum eServerSampleRate
{
    SERVERSAMPLERATE_NARROWBAND = 0,
    SERVERSAMPLERATE_MEDIUMBAND,
    SERVERSAMPLERATE_WIDEBAND,
    SERVERSAMPLERATE_SUPERWIDEBAND,
    SERVERSAMPLERATE_FULLBAND,
};

enum eChannel
{
    CHANNEL_MONO = 1,
    CHANNEL_STEREO = 2,
};

class CVoiceRecorder
{
public:
    CVoiceRecorder();
    ~CVoiceRecorder();

    void Init(bool bEnabled, unsigned int uiServerSampleRate, unsigned char ucComplexity, unsigned int uiBitrate);

    bool IsEnabled() const { return m_bEnabled; }

    void DoPulse();

    void SetPTTState(bool bState);
    bool GetPTTState() const { return m_VoiceState != VOICESTATE_AWAITING_INPUT; }

    unsigned int  GetSampleRate() const { return m_SampleRate; }
    unsigned char GetComputationalComplexity() const { return m_ucComplexity; }
    unsigned char GetChannels() const { return m_Channels; }

private:
    void DeInit();
    void SendFrame(const opus_int16* inputBuffer, unsigned int length);

    static BOOL CALLBACK BASSCallback(HRECORD handle, const void* buffer, DWORD length, void* user);
    static int           getOpusBandwidthFromSampleRate(unsigned int uiSampleRate);
    static eSampleRate   convertServerSampleRate(const unsigned int uiServerSampleRate);

    bool m_bEnabled;

    eVoiceState   m_VoiceState;
    eSampleRate   m_SampleRate;
    unsigned char m_ucComplexity;
    eChannel      m_Channels;

    HRECORD m_pRecordingHandle;            // bass recording handle

    OpusEncoder* m_pEncoderState;

    unsigned int m_uiFrameSize;
    unsigned int m_uiBufferLength;
    unsigned int m_uiBufferWriteIndex;
    unsigned int m_uiBufferReadIndex;

    opus_int16* m_pBuffer;
    opus_int16  m_pFrameBuffer[VOICE_MAX_FRAME_SIZE];

    unsigned long m_ulTimeOfLastSend;

    std::mutex m_mutex;            // so that we don't read and write at the same time
};
