/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/CVoiceRecorder.cpp
 *  PURPOSE:     Player voice recording, encoding and sending
 *
 *  Multi Theft Auto is available from http://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CVoiceRecorder.h"

CVoiceRecorder::CVoiceRecorder()
{
    m_bEnabled = false;

    m_VoiceState = VOICESTATE_AWAITING_INPUT;
    m_SampleRate = SAMPLERATE_WIDEBAND;
    m_ucComplexity = 8;
    m_Channels = CHANNEL_STEREO;

    m_pRecordingHandle = NULL;

    m_pEncoderState = nullptr;

    m_uiFrameSize = 0;
    m_uiBufferLength = 0;
    m_uiBufferWriteIndex = 0;
    m_uiBufferReadIndex = 0;

    m_pBuffer = nullptr;

    m_ulTimeOfLastSend = 0;
}

CVoiceRecorder::~CVoiceRecorder()
{
    DeInit();
}

BOOL CVoiceRecorder::BASSCallback(HRECORD handle, const void* inputBuffer, DWORD length, void* userData)
{
    // This assumes that BASSCallback will only be called when userData is a valid CVoiceRecorder pointer
    CVoiceRecorder* pVoiceRecorder = static_cast<CVoiceRecorder*>(userData);
    if (pVoiceRecorder->IsEnabled())
        pVoiceRecorder->SendFrame(static_cast<const opus_int16*>(inputBuffer), length / sizeof(opus_int16));
    return TRUE;            // continue recording
}

void CVoiceRecorder::Init(bool bEnabled, unsigned int uiServerSampleRate, unsigned char ucComplexity, unsigned int uiBitrate)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_bEnabled = bEnabled;

    if (!bEnabled)            // If we aren't enabled, don't bother continuing
        return;

    m_VoiceState = VOICESTATE_AWAITING_INPUT;
    m_SampleRate =
        convertServerSampleRate(uiServerSampleRate);            // Convert the sample rate we received from the server (0-2) into an actual sample rate
    m_ucComplexity = ucComplexity;
    m_Channels = CHANNEL_MONO;

    int err;

    // Initialize BASS
    BASS_RecordInit(-1);
    if ((err = BASS_ErrorGetCode()) != BASS_OK)
    {
        g_pCore->GetConsole()->Printf("[VOICE] BASS ERROR BASS_RecordInit: %i", err);
        return;
    }

    // Start recording with BASS
    m_pRecordingHandle = BASS_RecordStart(m_SampleRate, m_Channels, NULL, &BASSCallback, this);
    if ((err = BASS_ErrorGetCode()) != BASS_OK)
    {
        g_pCore->GetConsole()->Printf("[VOICE] BASS ERROR BASS_RecordStart: %i", err);
        DeInit();
        return;
    }

    DWORD           deviceid = BASS_RecordGetDevice();
    BASS_DEVICEINFO info;
    BASS_GetDeviceInfo(deviceid, &info);
    g_pCore->GetConsole()->Printf("[VOICE] Using device %s (%x)", info.name, info.flags);

    // Create Opus encoder
    m_pEncoderState = opus_encoder_create(m_SampleRate, m_Channels, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK)
    {
        g_pCore->GetConsole()->Printf("[VOICE] Opus encoder creation failed with error %i", err);
        DeInit();
        return;
    }

    // Set encoder parameters

    opus_encoder_ctl(m_pEncoderState, OPUS_SET_COMPLEXITY(m_ucComplexity));
    opus_encoder_ctl(m_pEncoderState, OPUS_SET_DTX(1));            // Discontinous Transmission
    opus_encoder_ctl(m_pEncoderState, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

    opus_int32 iBitrate = (opus_int32)uiBitrate;
    if (uiBitrate)
        opus_encoder_ctl(m_pEncoderState, OPUS_SET_BITRATE(iBitrate));
    else
        opus_encoder_ctl(m_pEncoderState, OPUS_GET_BITRATE(&iBitrate));

    // Set up the buffers
    m_uiFrameSize = m_SampleRate / 50;            // 20 ms frame size

    unsigned int fullFrameSize = m_uiFrameSize * m_Channels;
    m_uiBufferLength = fullFrameSize * VOICE_OUTGOING_FRAMES_COUNT;
    m_uiBufferWriteIndex = 0;
    m_uiBufferReadIndex = 0;

    m_pBuffer = new opus_int16[m_uiBufferLength] {0};            // Allocate the buffer

    memset(m_pFrameBuffer, 0, sizeof(m_pFrameBuffer));

    // Time of last send, this is used to limit sending
    m_ulTimeOfLastSend = 0;

    g_pCore->GetConsole()->Printf("Server Voice Chat: Computational Complexity [%i]; Sample Rate: [%iHz]; Bitrate [%ibps]", m_ucComplexity, m_SampleRate, iBitrate);
}

void CVoiceRecorder::DeInit()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_bEnabled = false;

    m_VoiceState = VOICESTATE_AWAITING_INPUT;
    m_SampleRate = SAMPLERATE_WIDEBAND;
    m_ucComplexity = 8;
    m_Channels = CHANNEL_STEREO;

    if (m_pRecordingHandle)
        BASS_ChannelStop(m_pRecordingHandle);
    m_pRecordingHandle = NULL;

    if (m_pEncoderState != nullptr)
        opus_encoder_destroy(m_pEncoderState);
    m_pEncoderState = nullptr;

    m_uiFrameSize = 0;
    m_uiBufferLength = 0;
    m_uiBufferWriteIndex = 0;
    m_uiBufferReadIndex = 0;

    if (m_pBuffer != nullptr)
        delete[] m_pBuffer;
    m_pBuffer = nullptr;

    if (m_pFrameBuffer != nullptr)
        delete[] m_pFrameBuffer;

    BASS_RecordFree();

    m_ulTimeOfLastSend = 0;
}

int CVoiceRecorder::getOpusBandwidthFromSampleRate(unsigned int uiSampleRate)
{
    switch (uiSampleRate)
    {
        case SAMPLERATE_NARROWBAND:
            return OPUS_BANDWIDTH_NARROWBAND;
        case SAMPLERATE_MEDIUMBAND:
            return OPUS_BANDWIDTH_MEDIUMBAND;
        case SAMPLERATE_WIDEBAND:
            return OPUS_BANDWIDTH_WIDEBAND;
        case SAMPLERATE_SUPERWIDEBAND:
            return OPUS_BANDWIDTH_SUPERWIDEBAND;
        case SAMPLERATE_FULLBAND:
            return OPUS_BANDWIDTH_FULLBAND;
        default:
            return OPUS_BANDWIDTH_WIDEBAND;
    }
}

eSampleRate CVoiceRecorder::convertServerSampleRate(unsigned int uiServerSampleRate)
{
    switch (uiServerSampleRate)
    {
        case SERVERSAMPLERATE_NARROWBAND:
            return SAMPLERATE_NARROWBAND;
        case SERVERSAMPLERATE_MEDIUMBAND:
            return SAMPLERATE_MEDIUMBAND;
        case SERVERSAMPLERATE_WIDEBAND:
            return SAMPLERATE_WIDEBAND;
        case SERVERSAMPLERATE_SUPERWIDEBAND:
            return SAMPLERATE_SUPERWIDEBAND;
        case SERVERSAMPLERATE_FULLBAND:
            return SAMPLERATE_FULLBAND;
        default:
            return SAMPLERATE_WIDEBAND;
    }
}

void CVoiceRecorder::SetPTTState(bool bState)
{
    if (!m_bEnabled)
        return;

    m_mutex.lock();            // manually locked as we need to unlock it before sending events

    if (bState)
    {
        if (m_VoiceState == VOICESTATE_AWAITING_INPUT)
        {
            // Call event on the local player for starting to talk
            if (g_pClientGame->GetLocalPlayer())
            {
                m_mutex.unlock();

                CLuaArguments Arguments;
                if (!g_pClientGame->GetLocalPlayer()->CallEvent("onClientVoiceStart", Arguments, true))
                    return;

                m_mutex.lock();

                if (m_VoiceState == VOICESTATE_AWAITING_INPUT)
                    m_VoiceState = VOICESTATE_RECORDING;
            }
        }
    }
    else
    {
        if (m_VoiceState == VOICESTATE_RECORDING)
        {
            m_VoiceState = VOICESTATE_RECORDING_LAST_PACKET;

            // Call event on the local player for stopping to talk
            if (g_pClientGame->GetLocalPlayer())
            {
                m_mutex.unlock();

                CLuaArguments Arguments;
                g_pClientGame->GetLocalPlayer()->CallEvent("onClientVoiceStop", Arguments, true);

                m_mutex.lock();
                m_mutex.unlock();
                return;
            }
        }
    }

    m_mutex.unlock();
}

void CVoiceRecorder::DoPulse()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Only send every 100 ms
    if (CClientTime::GetTime() - m_ulTimeOfLastSend > 100 && m_VoiceState != VOICESTATE_AWAITING_INPUT)
    {
        opus_int16*   pInputBuffer;
        unsigned char pOutBuffer[VOICE_MAX_PACKET_SIZE];

        // Calculate how much of our buffer has useful data
        unsigned int uiData;
        if (m_uiBufferReadIndex >= m_uiBufferWriteIndex)
            uiData = (m_uiBufferLength - m_uiBufferReadIndex) + m_uiBufferWriteIndex;
        else
            uiData = m_uiBufferWriteIndex - m_uiBufferReadIndex;

        unsigned int fullFrameSize = m_uiFrameSize * m_Channels;
        unsigned int uiFramesAvailable = uiData / fullFrameSize;

        if (uiFramesAvailable > 0)
        {
            while (uiFramesAvailable-- > 0)
            {
                if (m_uiBufferReadIndex + fullFrameSize >= m_uiBufferLength)
                {
                    // Data wraps around the buffer, copy it first
                    for (unsigned int t = 0; t < fullFrameSize; t++)
                        m_pFrameBuffer[t] = m_pBuffer[(m_uiBufferReadIndex + t) % m_uiBufferLength];
                    pInputBuffer = m_pFrameBuffer;
                }
                else
                    pInputBuffer = m_pBuffer + m_uiBufferReadIndex;

                // Advance the read index
                m_uiBufferReadIndex = (m_uiBufferReadIndex + fullFrameSize) % m_uiBufferLength;

                // Encode the input buffer
                opus_int16 len = opus_encode(m_pEncoderState, pInputBuffer, m_uiFrameSize, pOutBuffer, VOICE_MAX_PACKET_SIZE);
                if (len >= 3)            // Discontinous transmission (packets less than 3 bytes in size don't have to be transmitted)
                {
                    g_pClientGame->GetLocalPlayer()->GetVoice()->DecodeAndBuffer(pOutBuffer, len);

                    if (g_pClientGame->GetPlayerManager()->GetLocalPlayer())
                    {
                        NetBitStreamInterface* pBitStream = g_pNet->AllocateNetBitStream();
                        if (pBitStream)
                        {
                            pBitStream->Write((unsigned short)len);               // Size of buffer/voice data
                            pBitStream->Write((char*)pOutBuffer, len);            // Voice data

                            g_pNet->SendPacket(PACKET_ID_VOICE_DATA, pBitStream, PACKET_PRIORITY_LOW, PACKET_RELIABILITY_UNRELIABLE_SEQUENCED,
                                               PACKET_ORDERING_VOICE);
                            g_pNet->DeallocateNetBitStream(pBitStream);
                        }
                    }
                }
            }

            m_ulTimeOfLastSend = CClientTime::GetTime();
        }
    }

    // End of voice data (for events)
    if (m_VoiceState == VOICESTATE_RECORDING_LAST_PACKET)
    {
        m_VoiceState = VOICESTATE_AWAITING_INPUT;

        if (g_pClientGame->GetPlayerManager()->GetLocalPlayer())
        {
            NetBitStreamInterface* pBitStream = g_pNet->AllocateNetBitStream();
            if (pBitStream)
            {
                g_pNet->SendPacket(PACKET_ID_VOICE_END, pBitStream, PACKET_PRIORITY_LOW, PACKET_RELIABILITY_UNRELIABLE_SEQUENCED, PACKET_ORDERING_VOICE);
                g_pNet->DeallocateNetBitStream(pBitStream);
            }
        }
    }
}

void CVoiceRecorder::SendFrame(const opus_int16* inputBuffer, unsigned int uiLength)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_VoiceState == VOICESTATE_AWAITING_INPUT || !m_bEnabled || !inputBuffer)
        return;

    unsigned int uiRemaining;

    // Calculate how much of our buffer is remaining
    if (m_uiBufferWriteIndex >= m_uiBufferReadIndex)
        uiRemaining = (m_uiBufferLength - m_uiBufferWriteIndex) + m_uiBufferReadIndex;
    else
        uiRemaining = m_uiBufferReadIndex - m_uiBufferWriteIndex;

    // Wrap the data around the buffer
    for (unsigned int t = 0; t < uiLength; t++)
    {
        m_pBuffer[(m_uiBufferWriteIndex + t) % m_uiBufferLength] = inputBuffer[t];
    }

    // Advance the write index
    m_uiBufferWriteIndex = (m_uiBufferWriteIndex + uiLength) % m_uiBufferLength;

    return;
}
