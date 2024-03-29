/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2015 - ROLI Ltd.

   Permission is granted to use this software under the terms of either:
   a) the GPL v2 (or any later version)
   b) the Affero GPL v3

   Details of these licenses can be found at: www.gnu.org/licenses

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

   ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.juce.com for more information.

  ==============================================================================
*/

//==============================================================================
#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD) \
 STATICMETHOD (getMinBufferSize,            "getMinBufferSize",             "(III)I") \
 STATICMETHOD (getNativeOutputSampleRate,   "getNativeOutputSampleRate",    "(I)I") \
 METHOD (constructor,   "<init>",   "(IIIIII)V") \
 METHOD (getState,      "getState", "()I") \
 METHOD (play,          "play",     "()V") \
 METHOD (stop,          "stop",     "()V") \
 METHOD (release,       "release",  "()V") \
 METHOD (flush,         "flush",    "()V") \
 METHOD (write,         "write",    "([SII)I") \

DECLARE_JNI_CLASS (AudioTrack, "android/media/AudioTrack");
#undef JNI_CLASS_MEMBERS

//==============================================================================
#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD) \
 STATICMETHOD (getMinBufferSize, "getMinBufferSize", "(III)I") \
 METHOD (constructor,       "<init>",           "(IIIII)V") \
 METHOD (getState,          "getState",         "()I") \
 METHOD (startRecording,    "startRecording",   "()V") \
 METHOD (stop,              "stop",             "()V") \
 METHOD (read,              "read",             "([SII)I") \
 METHOD (release,           "release",          "()V") \

DECLARE_JNI_CLASS (AudioRecord, "android/media/AudioRecord");
#undef JNI_CLASS_MEMBERS

//==============================================================================
enum
{
    CHANNEL_OUT_STEREO  = 12,
    CHANNEL_IN_STEREO   = 12,
    CHANNEL_IN_MONO     = 16,
    ENCODING_PCM_16BIT  = 2,
    STREAM_MUSIC        = 3,
    MODE_STREAM         = 1,
    STATE_UNINITIALIZED = 0
};

const char* const javaAudioTypeName = "Android Audio";

//==============================================================================
class AndroidAudioIODevice  : public AudioIODevice,
                              public Thread
{
public:
    //==============================================================================
    AndroidAudioIODevice (const String& deviceName)
        : AudioIODevice (deviceName, javaAudioTypeName),
          Thread ("audio"),
          minBufferSizeOut (0), minBufferSizeIn (0), callback (0), sampleRate (0),
          numClientInputChannels (0), numDeviceInputChannels (0), numDeviceInputChannelsAvailable (2),
          numClientOutputChannels (0), numDeviceOutputChannels (0),
          actualBufferSize (0), isRunning (false),
          inputChannelBuffer (1, 1),
          outputChannelBuffer (1, 1)
    {
        JNIEnv* env = getEnv();
        sampleRate = env->CallStaticIntMethod (AudioTrack, AudioTrack.getNativeOutputSampleRate, MODE_STREAM);

        minBufferSizeOut = (int) env->CallStaticIntMethod (AudioTrack,  AudioTrack.getMinBufferSize,  sampleRate, CHANNEL_OUT_STEREO, ENCODING_PCM_16BIT);
        minBufferSizeIn  = (int) env->CallStaticIntMethod (AudioRecord, AudioRecord.getMinBufferSize, sampleRate, CHANNEL_IN_STEREO,  ENCODING_PCM_16BIT);

        if (minBufferSizeIn <= 0)
        {
            minBufferSizeIn = env->CallStaticIntMethod (AudioRecord, AudioRecord.getMinBufferSize, sampleRate, CHANNEL_IN_MONO, ENCODING_PCM_16BIT);

            if (minBufferSizeIn > 0)
                numDeviceInputChannelsAvailable = 1;
            else
                numDeviceInputChannelsAvailable = 0;
        }

        DBG ("Audio device - min buffers: " << minBufferSizeOut << ", " << minBufferSizeIn << "; "
              << sampleRate << " Hz; input chans: " << numDeviceInputChannelsAvailable);
    }

    ~AndroidAudioIODevice()
    {
        close();
    }

    StringArray getOutputChannelNames() override
    {
        StringArray s;
        s.add ("Left");
        s.add ("Right");
        return s;
    }

    StringArray getInputChannelNames() override
    {
        StringArray s;

        if (numDeviceInputChannelsAvailable == 2)
        {
            s.add ("Left");
            s.add ("Right");
        }
        else if (numDeviceInputChannelsAvailable == 1)
        {
            s.add ("Audio Input");
        }

        return s;
    }

    Array<double> getAvailableSampleRates() override
    {
        Array<double> r;
        r.add ((double) sampleRate);
        return r;
    }

    Array<int> getAvailableBufferSizes() override
    {
        Array<int> b;
        int n = 16;

        for (int i = 0; i < 50; ++i)
        {
            b.add (n);
            n += n < 64 ? 16
                        : (n < 512 ? 32
                                   : (n < 1024 ? 64
                                               : (n < 2048 ? 128 : 256)));
        }

        return b;
    }

    int getDefaultBufferSize() override                 { return 2048; }

    String open (const BigInteger& inputChannels,
                 const BigInteger& outputChannels,
                 double requestedSampleRate,
                 int bufferSize) override
    {
        close();

        if (sampleRate != (int) requestedSampleRate)
            return "Sample rate not allowed";

        lastError.clear();
        int preferredBufferSize = (bufferSize <= 0) ? getDefaultBufferSize() : bufferSize;

        numDeviceInputChannels = 0;
        numDeviceOutputChannels = 0;

        activeOutputChans = outputChannels;
        activeOutputChans.setRange (2, activeOutputChans.getHighestBit(), false);
        numClientOutputChannels = activeOutputChans.countNumberOfSetBits();

        activeInputChans = inputChannels;
        activeInputChans.setRange (2, activeInputChans.getHighestBit(), false);
        numClientInputChannels = activeInputChans.countNumberOfSetBits();

        actualBufferSize = preferredBufferSize;
        inputChannelBuffer.setSize (2, actualBufferSize);
        inputChannelBuffer.clear();
        outputChannelBuffer.setSize (2, actualBufferSize);
        outputChannelBuffer.clear();

        JNIEnv* env = getEnv();

        if (numClientOutputChannels > 0)
        {
            numDeviceOutputChannels = 2;
            outputDevice = GlobalRef (env->NewObject (AudioTrack, AudioTrack.constructor,
                                                      STREAM_MUSIC, sampleRate, CHANNEL_OUT_STEREO, ENCODING_PCM_16BIT,
                                                      (jint) (minBufferSizeOut * numDeviceOutputChannels * sizeof (int16)), MODE_STREAM));

            if (env->CallIntMethod (outputDevice, AudioTrack.getState) != STATE_UNINITIALIZED)
                isRunning = true;
            else
                outputDevice.clear(); // failed to open the device
        }

        if (numClientInputChannels > 0 && numDeviceInputChannelsAvailable > 0)
        {
            numDeviceInputChannels = jmin (numClientInputChannels, numDeviceInputChannelsAvailable);
            inputDevice = GlobalRef (env->NewObject (AudioRecord, AudioRecord.constructor,
                                                     0 /* (default audio source) */, sampleRate,
                                                     numDeviceInputChannelsAvailable > 1 ? CHANNEL_IN_STEREO : CHANNEL_IN_MONO,
                                                     ENCODING_PCM_16BIT,
                                                     (jint) (minBufferSizeIn * numDeviceInputChannels * sizeof (int16))));

            if (env->CallIntMethod (inputDevice, AudioRecord.getState) != STATE_UNINITIALIZED)
                isRunning = true;
            else
                inputDevice.clear(); // failed to open the device
        }

        if (isRunning)
        {
            if (outputDevice != nullptr)
                env->CallVoidMethod (outputDevice, AudioTrack.play);

            if (inputDevice != nullptr)
                env->CallVoidMethod (inputDevice, AudioRecord.startRecording);

            startThread (8);
        }
        else
        {
            closeDevices();
        }

        return lastError;
    }

    void close() override
    {
        if (isRunning)
        {
            stopThread (2000);
            isRunning = false;
            closeDevices();
        }
    }

    int getOutputLatencyInSamples() override             { return (minBufferSizeOut * 3) / 4; }
    int getInputLatencyInSamples() override              { return (minBufferSizeIn * 3) / 4; }
    bool isOpen() override                               { return isRunning; }
    int getCurrentBufferSizeSamples() override           { return actualBufferSize; }
    int getCurrentBitDepth() override                    { return 16; }
    double getCurrentSampleRate() override               { return sampleRate; }
    BigInteger getActiveOutputChannels() const override  { return activeOutputChans; }
    BigInteger getActiveInputChannels() const override   { return activeInputChans; }
    String getLastError() override                       { return lastError; }
    bool isPlaying() override                            { return isRunning && callback != 0; }

    void start (AudioIODeviceCallback* newCallback) override
    {
        if (isRunning && callback != newCallback)
        {
            if (newCallback != nullptr)
                newCallback->audioDeviceAboutToStart (this);

            const ScopedLock sl (callbackLock);
            callback = newCallback;
        }
    }

    void stop() override
    {
        if (isRunning)
        {
            AudioIODeviceCallback* lastCallback;

            {
                const ScopedLock sl (callbackLock);
                lastCallback = callback;
                callback = nullptr;
            }

            if (lastCallback != nullptr)
                lastCallback->audioDeviceStopped();
        }
    }

    void run() override
    {
        JNIEnv* env = getEnv();
        jshortArray audioBuffer = env->NewShortArray (actualBufferSize * jmax (numDeviceOutputChannels, numDeviceInputChannels));

        while (! threadShouldExit())
        {
            if (inputDevice != nullptr)
            {
                jint numRead = env->CallIntMethod (inputDevice, AudioRecord.read, audioBuffer, 0, actualBufferSize * numDeviceInputChannels);

                if (numRead < actualBufferSize * numDeviceInputChannels)
                {
                    DBG ("Audio read under-run! " << numRead);
                }

                jshort* const src = env->GetShortArrayElements (audioBuffer, 0);

                for (int chan = 0; chan < inputChannelBuffer.getNumChannels(); ++chan)
                {
                    AudioData::Pointer <AudioData::Float32, AudioData::NativeEndian, AudioData::NonInterleaved, AudioData::NonConst> d (inputChannelBuffer.getWritePointer (chan));

                    if (chan < numDeviceInputChannels)
                    {
                        AudioData::Pointer <AudioData::Int16, AudioData::NativeEndian, AudioData::Interleaved, AudioData::Const> s (src + chan, numDeviceInputChannels);
                        d.convertSamples (s, actualBufferSize);
                    }
                    else
                    {
                        d.clearSamples (actualBufferSize);
                    }
                }

                env->ReleaseShortArrayElements (audioBuffer, src, 0);
            }

            if (threadShouldExit())
                break;

            {
                const ScopedLock sl (callbackLock);

                if (callback != nullptr)
                {
                    callback->audioDeviceIOCallback (inputChannelBuffer.getArrayOfReadPointers(), numClientInputChannels,
                                                     outputChannelBuffer.getArrayOfWritePointers(), numClientOutputChannels,
                                                     actualBufferSize);
                }
                else
                {
                    outputChannelBuffer.clear();
                }
            }

            if (outputDevice != nullptr)
            {
                if (threadShouldExit())
                    break;

                jshort* const dest = env->GetShortArrayElements (audioBuffer, 0);

                for (int chan = 0; chan < numDeviceOutputChannels; ++chan)
                {
                    AudioData::Pointer <AudioData::Int16, AudioData::NativeEndian, AudioData::Interleaved, AudioData::NonConst> d (dest + chan, numDeviceOutputChannels);

                    const float* const sourceChanData = outputChannelBuffer.getReadPointer (jmin (chan, outputChannelBuffer.getNumChannels() - 1));
                    AudioData::Pointer <AudioData::Float32, AudioData::NativeEndian, AudioData::NonInterleaved, AudioData::Const> s (sourceChanData);
                    d.convertSamples (s, actualBufferSize);
                }

                env->ReleaseShortArrayElements (audioBuffer, dest, 0);
                jint numWritten = env->CallIntMethod (outputDevice, AudioTrack.write, audioBuffer, 0, actualBufferSize * numDeviceOutputChannels);

                if (numWritten < actualBufferSize * numDeviceOutputChannels)
                {
                    DBG ("Audio write underrun! " << numWritten);
                }
            }
        }
    }

    int minBufferSizeOut, minBufferSizeIn;

private:
    //==============================================================================
    CriticalSection callbackLock;
    AudioIODeviceCallback* callback;
    jint sampleRate;
    int numClientInputChannels, numDeviceInputChannels, numDeviceInputChannelsAvailable;
    int numClientOutputChannels, numDeviceOutputChannels;
    int actualBufferSize;
    bool isRunning;
    String lastError;
    BigInteger activeOutputChans, activeInputChans;
    GlobalRef outputDevice, inputDevice;
    AudioSampleBuffer inputChannelBuffer, outputChannelBuffer;

    void closeDevices()
    {
        if (outputDevice != nullptr)
        {
            outputDevice.callVoidMethod (AudioTrack.stop);
            outputDevice.callVoidMethod (AudioTrack.release);
            outputDevice.clear();
        }

        if (inputDevice != nullptr)
        {
            inputDevice.callVoidMethod (AudioRecord.stop);
            inputDevice.callVoidMethod (AudioRecord.release);
            inputDevice.clear();
        }
    }

    JUCE_DECLARE_NON_COPYABLE (AndroidAudioIODevice)
};

//==============================================================================
class AndroidAudioIODeviceType  : public AudioIODeviceType
{
public:
    AndroidAudioIODeviceType()  : AudioIODeviceType (javaAudioTypeName) {}

    //==============================================================================
    void scanForDevices() {}
    StringArray getDeviceNames (bool wantInputNames) const              { return StringArray (javaAudioTypeName); }
    int getDefaultDeviceIndex (bool forInput) const                     { return 0; }
    int getIndexOfDevice (AudioIODevice* device, bool asInput) const    { return device != nullptr ? 0 : -1; }
    bool hasSeparateInputsAndOutputs() const                            { return false; }

    AudioIODevice* createDevice (const String& outputDeviceName,
                                 const String& inputDeviceName)
    {
        ScopedPointer<AndroidAudioIODevice> dev;

        if (outputDeviceName.isNotEmpty() || inputDeviceName.isNotEmpty())
        {
            dev = new AndroidAudioIODevice (outputDeviceName.isNotEmpty() ? outputDeviceName
                                                                          : inputDeviceName);

            if (dev->getCurrentSampleRate() <= 0 || dev->getDefaultBufferSize() <= 0)
                dev = nullptr;
        }

        return dev.release();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AndroidAudioIODeviceType)
};


//==============================================================================
extern bool isOpenSLAvailable();

AudioIODeviceType* AudioIODeviceType::createAudioIODeviceType_Android()
{
   #if JUCE_USE_ANDROID_OPENSLES
    if (isOpenSLAvailable())
        return nullptr;
   #endif

    return new AndroidAudioIODeviceType();
}
