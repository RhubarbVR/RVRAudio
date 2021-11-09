#include "pch.h"
#include "RVRAudioNative.h"
using namespace System;
using namespace std;
namespace RVRAudioNative
{

	SpinLock::SpinLock()
	{
		mLocked = false;
	}

	void SpinLock::Lock()
	{
		while (mLocked) {}
		mLocked = true;
	}

	void SpinLock::Unlock()
	{
		mLocked = false;
	}





	void* OpenALLibrary = NULL;

	class ContextState
	{
	public:
		ContextState(ALCcontext* context)
		{
			sOpenAlLock.Lock();

			mOldContext = alcGetCurrentContext();
			if (context != mOldContext)
			{
				alcMakeContextCurrent(context);
				swap = true;
			}
			else
			{
				swap = false;
			}
		}

		~ContextState()
		{
			if (swap)
			{
				alcMakeContextCurrent(mOldContext);
			}

			sOpenAlLock.Unlock();
		}

	private:
		bool swap;
		ALCcontext* mOldContext;
		static SpinLock sOpenAlLock;
	};

	SpinLock ContextState::sOpenAlLock;


#define AL_ERROR //if (auto err = GetErrorAL() != AL_NO_ERROR) debugtrap()
#define ALC_ERROR(__device__) //if (auto err = GetErrorALC(__device__) != ALC_NO_ERROR) debugtrap()


	rvrAudioDevice* NativeAudio::rvrAudioCreate(const char* deviceName, int flags)
	{
		auto res = new rvrAudioDevice;
		res->device = alcOpenDevice(deviceName);
		ALC_ERROR(res->device);
		if (!res->device)
		{
			delete res;
			return NULL;
		}
		return res;
	}

	void NativeAudio::rvrAudioDestroy(rvrAudioDevice* device)
	{
		alcCloseDevice(device->device);
		ALC_ERROR(device->device);
		delete device;
	}

	void NativeAudio::rvrAudioUpdate(rvrAudioDevice* device)
	{
		device->deviceLock.Lock();

		for (auto listener : device->listeners)
		{
			ContextState lock(listener->context);

			for (auto source : listener->sources)
			{
				if (source->streamed)
				{
					auto processed = 0;
					alGetSourcei(source->source, AL_BUFFERS_PROCESSED, &processed);
					while (processed--)
					{
						ALfloat preDTime;
						alGetSourcef(source->source, AL_SEC_OFFSET, &preDTime);

						ALuint buffer;
						alSourceUnqueueBuffers(source->source, 1, &buffer);
						rvrAudioBuffer* bufferPtr = source->listener->buffers[buffer];

						ALfloat postDTime;
						alGetSourcef(source->source, AL_SEC_OFFSET, &postDTime);

						if (bufferPtr->type == EndOfStream || bufferPtr->type == EndOfLoop)
						{
							source->dequeuedTime = 0.0;
						}
						else
						{
							source->dequeuedTime += preDTime - postDTime;
						}

						source->freeBuffers.push_back(bufferPtr);
					}
				}
			}
		}

		device->deviceLock.Unlock();
	}

	rvrAudioListener* NativeAudio::rvrAudioListenerCreate(rvrAudioDevice* device)
	{
		auto res = new rvrAudioListener;
		res->device = device;

		res->context = alcCreateContext(device->device, NULL);
		ALC_ERROR(device->device);
		alcMakeContextCurrent(res->context);
		ALC_ERROR(device->device);
		alcProcessContext(res->context);
		ALC_ERROR(device->device);

		device->deviceLock.Lock();

		device->listeners.insert(res);

		device->deviceLock.Unlock();

		return res;
	}

	void NativeAudio::rvrAudioListenerDestroy(rvrAudioListener* listener)
	{
		listener->device->deviceLock.Lock();

		listener->device->listeners.erase(listener);

		listener->device->deviceLock.Unlock();

		alcDestroyContext(listener->context);

		delete listener;
	}

	void NativeAudio::rvrAudioSetMasterVolume(rvrAudioDevice* device, float volume)
	{
		device->deviceLock.Lock();
		for (auto listener : device->listeners)
		{
			ContextState lock(listener->context);
			alListenerf(AL_GAIN, volume);
		}
		device->deviceLock.Unlock();
	}

	bool NativeAudio::rvrAudioListenerEnable(rvrAudioListener* listener)
	{
		bool res = alcMakeContextCurrent(listener->context);
		alcProcessContext(listener->context);
		return res;
	}

	void NativeAudio::rvrAudioListenerDisable(rvrAudioListener* listener)
	{
		alcSuspendContext(listener->context);
		alcMakeContextCurrent(NULL);
	}

	rvrAudioSource* NativeAudio::rvrAudioSourceCreate(rvrAudioListener* listener, int sampleRate, int maxNBuffers, bool mono, bool spatialized, bool streamed, bool hrtf, float directionFactor, int environment)
	{
		(void)spatialized;
		(void)maxNBuffers;

		auto res = new rvrAudioSource;
		res->listener = listener;
		res->sampleRate = sampleRate;
		res->mono = mono;
		res->streamed = streamed;

		ContextState lock(listener->context);

		alGenSources(1, &res->source);
		AL_ERROR;
		alSourcef(res->source, AL_REFERENCE_DISTANCE, 1.0f);
		AL_ERROR;

		if (spatialized)
		{
			//make sure we are able to 3D
			alSourcei(res->source, AL_SOURCE_RELATIVE, AL_FALSE);
		}
		else
		{
			//make sure we are able to pan
			alSourcei(res->source, AL_SOURCE_RELATIVE, AL_TRUE);
		}

		listener->sources.insert(res);

		return res;
	}

	void NativeAudio::rvrAudioSourceDestroy(rvrAudioSource* source)
	{
		ContextState lock(source->listener->context);

		alDeleteSources(1, &source->source);
		AL_ERROR;

		source->listener->sources.erase(source);

		delete source;
	}

	double NativeAudio::rvrAudioSourceGetPosition(rvrAudioSource* source)
	{
		ContextState lock(source->listener->context);

		ALfloat offset;
		alGetSourcef(source->source, AL_SEC_OFFSET, &offset);

		if (!source->streamed)
		{
			return offset;
		}

		return offset + source->dequeuedTime;
	}

	void NativeAudio::rvrAudioSourceSetPan(rvrAudioSource* source, float pan)
	{
		auto clampedPan = pan > 1.0f ? 1.0f : pan < -1.0f ? -1.0f : pan;
		ALfloat alpan[3];
		alpan[0] = clampedPan; // from -1 (left) to +1 (right) 
		alpan[1] = sqrt(1.0f - clampedPan * clampedPan);
		alpan[2] = 0.0f;

		ContextState lock(source->listener->context);

		alSourcefv(source->source, AL_POSITION, alpan);
	}

	void NativeAudio::rvrAudioSourceSetLooping(rvrAudioSource* source, bool looping)
	{
		ContextState lock(source->listener->context);

		alSourcei(source->source, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
	}

	void NativeAudio::rvrAudioSourceSetRange(rvrAudioSource* source, double startTime, double stopTime)
	{
		if (source->streamed)
		{
			return;
		}

		ContextState lock(source->listener->context);

		ALint playing;
		alGetSourcei(source->source, AL_SOURCE_STATE, &playing);
		if (playing == AL_PLAYING) alSourceStop(source->source);
		alSourcei(source->source, AL_BUFFER, 0);

		//OpenAL is kinda bad and offers only starting offset...
		//As result we need to rewrite the buffer
		if (startTime == 0 && stopTime == 0)
		{
			//cancel the offsetting							
			alBufferData(source->singleBuffer->buffer, source->mono ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16, source->singleBuffer->pcm, source->singleBuffer->size, source->singleBuffer->sampleRate);
		}
		else
		{
			//offset the data
			auto sampleStart = int(double(source->singleBuffer->sampleRate) * (source->mono ? 1.0 : 2.0) * startTime);
			auto sampleStop = int(double(source->singleBuffer->sampleRate) * (source->mono ? 1.0 : 2.0) * stopTime);

			if (sampleStart > source->singleBuffer->size / sizeof(short))
			{
				return; //the starting position must be less then the total length of the buffer
			}

			if (sampleStop > source->singleBuffer->size / sizeof(short)) //if the end point is more then the length of the buffer fix the value
			{
				sampleStop = source->singleBuffer->size / sizeof(short);
			}

			auto len = sampleStop - sampleStart;

			auto offsettedBuffer = source->singleBuffer->pcm + sampleStart;

			alBufferData(source->singleBuffer->buffer, source->mono ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16, (void*)offsettedBuffer, len * sizeof(short), source->singleBuffer->sampleRate);
		}

		alSourcei(source->source, AL_BUFFER, source->singleBuffer->buffer);
		if (playing == AL_PLAYING) alSourcePlay(source->source);
	}

	void NativeAudio::rvrAudioSourceSetGain(rvrAudioSource* source, float gain)
	{
		ContextState lock(source->listener->context);

		alSourcef(source->source, AL_GAIN, gain);
	}

	void NativeAudio::rvrAudioSourceSetPitch(rvrAudioSource* source, float pitch)
	{
		ContextState lock(source->listener->context);

		alSourcef(source->source, AL_PITCH, pitch);
	}

	void NativeAudio::rvrAudioSourceSetBuffer(rvrAudioSource* source, rvrAudioBuffer* buffer)
	{
		ContextState lock(source->listener->context);

		source->singleBuffer = buffer;
		alSourcei(source->source, AL_BUFFER, buffer->buffer);
	}

	void NativeAudio::rvrAudioSourceQueueBuffer(rvrAudioSource* source, rvrAudioBuffer* buffer, short* pcm, int bufferSize, BufferType type)
	{
		ContextState lock(source->listener->context);

		buffer->type = type;
		buffer->size = bufferSize;
		alBufferData(buffer->buffer, source->mono ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16, pcm, bufferSize, source->sampleRate);
		alSourceQueueBuffers(source->source, 1, &buffer->buffer);
		source->listener->buffers[buffer->buffer] = buffer;
	}

	rvrAudioBuffer* NativeAudio::rvrAudioSourceGetFreeBuffer(rvrAudioSource* source)
	{
		ContextState lock(source->listener->context);

		if (source->freeBuffers.size() > 0)
		{
			auto buffer = source->freeBuffers.back();
			source->freeBuffers.pop_back();
			return buffer;
		}

		return NULL;
	}

	void NativeAudio::rvrAudioSourcePlay(rvrAudioSource* source)
	{
		ContextState lock(source->listener->context);

		alSourcePlay(source->source);
	}

	void NativeAudio::rvrAudioSourcePause(rvrAudioSource* source)
	{
		ContextState lock(source->listener->context);

		alSourcePause(source->source);
	}

	void NativeAudio::rvrAudioSourceFlushBuffers(rvrAudioSource* source)
	{
		ContextState lock(source->listener->context);

		if (source->streamed)
		{
			//flush all buffers
			auto processed = 0;
			alGetSourcei(source->source, AL_BUFFERS_PROCESSED, &processed);
			while (processed--)
			{
				ALuint buffer;
				alSourceUnqueueBuffers(source->source, 1, &buffer);
			}

			//return the source to undetermined mode
			alSourcei(source->source, AL_BUFFER, 0);

			//set all buffers as free
			source->freeBuffers.clear();
			for (auto buffer : source->listener->buffers)
			{
				source->freeBuffers.push_back(buffer.second);
			}
		}
	}

	void NativeAudio::rvrAudioSourceStop(rvrAudioSource* source)
	{
		ContextState lock(source->listener->context);

		alSourceStop(source->source);
		rvrAudioSourceFlushBuffers(source);

		//reset timing info
		if (source->streamed)
			source->dequeuedTime = 0.0;
	}

	void NativeAudio::rvrAudioListenerPush3D(rvrAudioListener* listener, float* pos, float* forward, float* up, float* vel)
	{
		ContextState lock(listener->context);

		if (forward && up)
		{
			float ori[6];
			ori[0] = forward[0];
			ori[1] = forward[1];
			ori[2] = -forward[2];
			ori[3] = up[0];
			ori[4] = up[1];
			ori[5] = -up[2];
			alListenerfv(AL_ORIENTATION, ori);
		}

		if (pos)
		{
			float pos2[3];
			pos2[0] = pos[0];
			pos2[1] = pos[1];
			pos2[2] = -pos[2];
			alListenerfv(AL_POSITION, pos2);
		}

		if (vel)
		{
			float vel2[3];
			vel2[0] = vel[0];
			vel2[1] = vel[1];
			vel2[2] = -vel[2];
			alListenerfv(AL_VELOCITY, vel2);
		}
	}

	void NativeAudio::rvrAudioSourcePush3D(rvrAudioSource* source, float* pos, float* forward, float* up, float* vel)
	{
		ContextState lock(source->listener->context);

		if (forward && up)
		{
			float ori[6];
			ori[0] = forward[0];
			ori[1] = forward[1];
			ori[2] = -forward[2];
			ori[3] = up[0];
			ori[4] = up[1];
			ori[5] = -up[2];
			alSourcefv(source->source, AL_ORIENTATION, ori);
		}

		if (pos)
		{
			float pos2[3];
			pos2[0] = pos[0];
			pos2[1] = pos[1];
			pos2[2] = -pos[2];
			alSourcefv(source->source, AL_POSITION, pos2);
		}

		if (vel)
		{
			float vel2[3];
			vel2[0] = vel[0];
			vel2[1] = vel[1];
			vel2[2] = -vel[2];
			alSourcefv(source->source, AL_VELOCITY, vel2);
		}
	}

	bool NativeAudio::rvrAudioSourceIsPlaying(rvrAudioSource* source)
	{
		ContextState lock(source->listener->context);

		ALint value;
		alGetSourcei(source->source, AL_SOURCE_STATE, &value);
		return value == AL_PLAYING || value == AL_PAUSED;
	}

	rvrAudioBuffer* NativeAudio::rvrAudioBufferCreate(int maxBufferSize)
	{
		auto res = new rvrAudioBuffer;
		res->pcm = (short*)malloc(maxBufferSize);
		alGenBuffers(1, &res->buffer);
		return res;
	}

	void NativeAudio::rvrAudioBufferDestroy(rvrAudioBuffer* buffer)
	{
		alDeleteBuffers(1, &buffer->buffer);
		free(buffer->pcm);
		delete buffer;
	}

	void NativeAudio::rvrAudioBufferFill(rvrAudioBuffer* buffer, short* pcm, int bufferSize, int sampleRate, bool mono)
	{
		//we have to keep a copy sadly because we might need to offset the data at some point			
		memcpy(buffer->pcm, pcm, bufferSize);
		buffer->size = bufferSize;
		buffer->sampleRate = sampleRate;

		alBufferData(buffer->buffer, mono ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16, pcm, bufferSize, sampleRate);
	}

}
