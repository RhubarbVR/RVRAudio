#pragma once
#include "pch.h"

namespace RVRAudioNative {
	public enum BufferType
	{
		None,
		BeginOfStream,
		EndOfStream,
		EndOfLoop
	};

	public class SpinLock {
	public:
		SpinLock();

		void Lock();

		void Unlock();
		volatile bool mLocked;
	};
	struct rvrAudioSource;
	struct rvrAudioListener;

	public struct rvrAudioBuffer
	{
		short* pcm = NULL;
		int size;
		int sampleRate;
		ALuint buffer;
		BufferType type;
	};

	public struct rvrAudioDevice
	{
		ALCdevice* device;
		SpinLock deviceLock;
		std::unordered_set<rvrAudioListener*> listeners;
	};

	public struct rvrAudioListener
	{
		rvrAudioDevice* device;
		ALCcontext* context;
		std::unordered_set<rvrAudioSource*> sources;
		std::unordered_map<ALuint, rvrAudioBuffer*> buffers;
	};

	public struct rvrAudioSource
	{
		ALuint source;
		int sampleRate;
		bool mono;
		bool streamed;

		volatile double dequeuedTime = 0.0;
		rvrAudioListener* listener;

		rvrAudioBuffer* singleBuffer;
		std::vector<rvrAudioBuffer*> freeBuffers;
	};

	public ref class NativeAudio {
	public:
		static rvrAudioDevice* rvrAudioCreate(const char* deviceName, int flags);

		static void rvrAudioDestroy(rvrAudioDevice* device);

		static void rvrAudioUpdate(rvrAudioDevice* device);

		static rvrAudioListener* rvrAudioListenerCreate(rvrAudioDevice* device);

		static void rvrAudioListenerDestroy(rvrAudioListener* listener);

		static void rvrAudioSetMasterVolume(rvrAudioDevice* device, float volume);

		static void rvrAudioListenerDisable(rvrAudioListener* listener);

		static bool rvrAudioListenerEnable(rvrAudioListener* listener);

		static rvrAudioSource* rvrAudioSourceCreate(rvrAudioListener* listener, int sampleRate, int maxNBuffers, bool mono, bool spatialized, bool streamed, bool hrtf, float directionFactor, int environment);

		static void rvrAudioSourceDestroy(rvrAudioSource* source);

		static double rvrAudioSourceGetPosition(rvrAudioSource* source);

		static void rvrAudioSourceSetPan(rvrAudioSource* source, float pan);

		static void rvrAudioSourceSetLooping(rvrAudioSource* source, bool looping);

		static void rvrAudioSourceSetRange(rvrAudioSource* source, double startTime, double stopTime);

		static void rvrAudioSourceSetGain(rvrAudioSource* source, float gain);

		static void rvrAudioSourceSetPitch(rvrAudioSource* source, float pitch);

		static void rvrAudioSourceSetBuffer(rvrAudioSource* source, rvrAudioBuffer* buffer);

		static void rvrAudioSourceQueueBuffer(rvrAudioSource* source, rvrAudioBuffer* buffer, short* pcm, int bufferSize, BufferType type);

		static rvrAudioBuffer* rvrAudioSourceGetFreeBuffer(rvrAudioSource* source);

		static void rvrAudioSourcePlay(rvrAudioSource* source);

		static void rvrAudioSourcePause(rvrAudioSource* source);

		static void rvrAudioSourceFlushBuffers(rvrAudioSource* source);

		static void rvrAudioSourceStop(rvrAudioSource* source);

		static void rvrAudioListenerPush3D(rvrAudioListener* listener, float* pos, float* forward, float* up, float* vel);

		static void rvrAudioSourcePush3D(rvrAudioSource* source, float* pos, float* forward, float* up, float* vel);

		static bool rvrAudioSourceIsPlaying(rvrAudioSource* source);

		static rvrAudioBuffer* rvrAudioBufferCreate(int maxBufferSize);

		static void rvrAudioBufferDestroy(rvrAudioBuffer* buffer);

		static void rvrAudioBufferFill(rvrAudioBuffer* buffer, short* pcm, int bufferSize, int sampleRate, bool mono);
	};
}