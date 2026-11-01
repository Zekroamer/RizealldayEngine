#pragma once
#include "audio/SpatialAudio.h"
#include <xaudio2.h>
#include <wrl.h>
#include <array>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

#pragma comment(lib, "xaudio2.lib")

/// Mixer groups; each has its own volume. Same values as ScriptAudioBus in ScriptAPI.h.
enum class AudioBus : int { Effects, Music, Voice, UI, Count };

class Audio
{
public:
	static Audio* GetInstance();

	struct PlayParams {
		bool loop = false;
		float volume = 1.0f;
		float pitch = 1.0f; // 0.25 .. 4, changes speed too
		AudioBus bus = AudioBus::Effects;
		bool spatial = false;
		Vector3 position{ 0.0f, 0.0f, 0.0f };
		float minDistance = 1.0f; // full volume up to here
		float maxDistance = 50.0f; // silent from here on
	};

	struct SoundData {
		WAVEFORMATEX wfex{};
		std::vector<BYTE> buffer;
		unsigned int bufferSize = 0;
		std::string filePath;
	};

	void Initialize();
	void Update();
	void Finalize();

	/// Load audio file (.wav, .mp3, .m4a, .wma) via Media Foundation (cached: loads from disk only once per file)
	uint32_t LoadAudio(const std::string& filename);

	/// Play cached audio by handle (no file I/O; uses in-memory decoded PCM buffer)
	/// @return Active voice handle (can be passed to StopAudio / SetVolume / IsPlaying)
	uint32_t PlayAudio(uint32_t soundHandle, bool loop = false, float volume = 1.0f);
	uint32_t PlayAudio(uint32_t soundHandle, const PlayParams& params);

	/// False on machines without an audio device (every play call then returns 0)
	bool IsAvailable() const { return xAudio2_ != nullptr; }

	void SetVoicePitch(uint32_t voiceHandle, float pitch);
	float GetVoicePitch(uint32_t voiceHandle) const; // 0 = no such voice
	/// Moves a 3D voice; heard after the next ApplySpatial
	void SetVoicePosition(uint32_t voiceHandle, const Vector3& position);
	/// Current level of each ear before volume and bus (false = no such voice)
	bool GetVoiceLevels(uint32_t voiceHandle, float* left, float* right) const;

	/// The ears, normally the game camera. Call once a frame, then ApplySpatial.
	void SetListener(const Vector3& position, const Vector3& right);
	/// Recomputes both ears of every 3D voice
	void ApplySpatial();

	void SetBusVolume(AudioBus bus, float volume);
	float GetBusVolume(AudioBus bus) const;
	void ResetBusVolumes();

	/// A new voice over a limit replaces another one: the oldest of the same sound, else the quietest effect
	void SetVoiceLimits(int maxVoices, int maxVoicesPerSound);
	int GetActiveVoiceCount() const { return static_cast<int>(activeVoices_.size()); }

	/// Stop a specific playing voice
	void StopAudio(uint32_t voiceHandle);

	/// Stop all playing voices
	void StopAllAudio();

	/// Adjust volume of a playing voice (0.0f ~ 1.0f+)
	void SetVolume(uint32_t voiceHandle, float volume);

	/// Check if a voice handle is currently playing
	bool IsPlaying(uint32_t voiceHandle) const;

	/// Load audio file into SoundData (uses Media Foundation + internal cache)
	SoundData SoundLoadWave(const std::string& filename);

	/// Play audio from SoundData
	uint32_t SoundPlayWave(const SoundData& soundData, bool loop = false, float volume = 1.0f);

	/// Release SoundData buffer
	void SoundUnload(SoundData* soundData);

private:
	Audio() = default;
	~Audio() = default;
	Audio(const Audio&) = delete;
	Audio& operator=(const Audio&) = delete;

	struct ActiveVoice {
		uint32_t voiceHandle = 0;
		uint32_t soundHandle = 0;
		IXAudio2SourceVoice* sourceVoice = nullptr;
		bool loop = false;
		AudioBus bus = AudioBus::Effects;
		uint32_t channels = 1;
		float volume = 1.0f;
		float gain = 1.0f; // distance gain of the last ApplySpatial
		bool spatial = false;
		Vector3 position{ 0.0f, 0.0f, 0.0f };
		float minDistance = 1.0f;
		float maxDistance = 50.0f;
	};

	void ApplySpatial(ActiveVoice& voice);
	void DestroyBuses();
	IXAudio2SubmixVoice* BusVoice(AudioBus bus) const { return busVoices_[static_cast<size_t>(bus)]; }

	static std::string NormalizePath(const std::string& filePath);
	static std::string ResolveFilePath(const std::string& filename);
	bool DecodeWithMediaFoundation(const std::string& resolvedPath, SoundData& outData);
	bool DecodeWaveFallback(const std::string& resolvedPath, SoundData& outData);

	Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
	IXAudio2MasteringVoice* masterVoice_ = nullptr;
	std::array<IXAudio2SubmixVoice*, static_cast<size_t>(AudioBus::Count)> busVoices_{};
	std::array<float, static_cast<size_t>(AudioBus::Count)> busVolumes_{ 1.0f, 1.0f, 1.0f, 1.0f };
	SpatialAudio::Listener listener_;
	int maxVoices_ = 64;
	int maxVoicesPerSound_ = 12;
	bool mfInitialized_ = false;

	uint32_t nextSoundHandle_ = 1;
	uint32_t nextVoiceHandle_ = 1;
	std::unordered_map<std::string, uint32_t> soundCache_;
	std::unordered_map<uint32_t, SoundData> soundDataMap_;
	std::vector<ActiveVoice> activeVoices_;
};
